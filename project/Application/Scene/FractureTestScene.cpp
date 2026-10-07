#include "FractureTestScene.h"
#include "Debug/DebugCamera.h"
#include "Effect/MeshFracture.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Math/MathUtil.h"
#include "Renderer/Object3dRenderer.h"
#include <cmath>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 元メッシュ（一辺 1 の凸な Cube）
const char *const kSourceModelPath = "Cube.obj";

// Cube.obj 標準のテクスチャはほぼ黒で陰影が見えないため、格子模様に差し替える
// 格子模様なら切り口付近の UV 補間が正しいかも目で確認できる
const char *const kCheckerTexturePath = "resources/uvChecker.png";

// カメラの初期位置（原点の Cube を斜め上から見下ろす）
const Vector3 kCameraStartPosition = {0.0f, 2.0f, -6.0f};

// ライトの向き（光が進む方向）
// 面ごとに明るさが変わるよう斜めにし、カメラから見える前面・上面・左面に当てる
const Vector3 kLightDirection = {0.4f, -1.0f, 0.6f};

// 切断平面の法線の初期値
// 軸に沿わない斜めの向きにして、断面が三角形や五角形になる場合も確認できるようにする
const Vector3 kDefaultPlaneNormal = {0.3f, 1.0f, 0.2f};

// 破片を切り口から離す距離の初期値
const float kDefaultSeparation = 0.3f;

// 体積の誤差の許容値（float の丸め誤差程度なら一致とみなす）
const float kVolumeTolerance = 1e-4f;
} // namespace

FractureTestScene::~FractureTestScene() = default;

void FractureTestScene::Initialize(EngineBase *engine) {

  // 基底クラスの初期化（PostProcessの生成など）
  BaseScene::Initialize(engine);

  // 参照をコピー
  engine_ = engine;

  // デバッグカメラ
  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize(kCameraStartPosition);
  engine_->GetObject3dRenderer()->SetDefaultCamera(debugCamera_->GetCamera());

  // ライトとフォグはレンダラー共通の状態で、前のシーンの設定が残るため上書きする
  if (auto *dl = engine_->GetObject3dRenderer()->GetDirectionalLightData()) {
    dl->color = {1.0f, 1.0f, 1.0f, 1.0f};
    dl->direction = Normalize(kLightDirection);
    dl->intensity = 1.0f;
  }
  FogData fog{};
  fog.enabled = 0.0f;
  engine_->GetObject3dRenderer()->SetFog(fog);

  // 元メッシュ（ModelManager の共有データはそのまま残し、コピーのテクスチャだけ変える）
  ModelManager *modelManager = ModelManager::GetInstance();
  modelManager->LoadModel(kSourceModelPath);
  Model::ModelData sourceData =
      modelManager->FindModel(kSourceModelPath)->GetModelData();
  sourceData.material.textureFilePath = kCheckerTexturePath;

  sourceModel_ = std::make_unique<Model>();
  sourceModel_->InitializeFromModelData(modelManager->GetModelRenderer(),
                                        sourceData);

  sourceObject_ = std::make_unique<Object3d>();
  sourceObject_->Initialize(engine_->GetObject3dRenderer());
  sourceObject_->SetModel(sourceModel_.get());
  sourceVolume_ = MeshFracture::ComputeVolume(sourceData);

  // 破片
  for (Fragment &fragment : fragments_) {
    fragment.object = std::make_unique<Object3d>();
    fragment.object->Initialize(engine_->GetObject3dRenderer());
  }
  planeNormal_ = kDefaultPlaneNormal;
  separation_ = kDefaultSeparation;
  RebuildFragments();
}

void FractureTestScene::Finalize() {}

void FractureTestScene::Update() {

  // カメラの更新
  debugCamera_->Update(*engine_->GetInputManager());
  engine_->GetObject3dRenderer()->SetDefaultCamera(debugCamera_->GetCamera());

#ifdef USE_IMGUI
  if (DrawDebugUI()) {
    RebuildFragments();
  }
#endif // USE_IMGUI

  // 破片を切り口から離して置く
  for (Fragment &fragment : fragments_) {
    fragment.object->SetTranslation(fragment.separationDirection * separation_);
    fragment.object->Update();
  }

  sourceObject_->Update();
}

void FractureTestScene::RebuildFragments() {

  // 法線の長さが 0 だと平面にならないので、作り直さない
  if (LengthSq(SafeNormalize(planeNormal_)) == 0.0f) {
    return;
  }

  // 表側の破片は平面そのまま、裏側の破片は裏返した平面で切る
  const MeshFracture::Plane plane =
      MeshFracture::MakePlane(planeNormal_, planePoint_);
  const std::array<MeshFracture::Plane, kFragmentCount_> planes = {
      plane, MeshFracture::FlipPlane(plane)};

  const Model::ModelData &source = sourceModel_->GetModelData();
  for (size_t i = 0; i < kFragmentCount_; ++i) {
    Fragment &fragment = fragments_[i];
    Model::ModelData data = MeshFracture::ClipMesh(source, planes[i]);
    fragment.volume = MeshFracture::ComputeVolume(data);
    fragment.separationDirection = planes[i].normal;

    // 古い GPU バッファはここで解放される
    // このエンジンは毎フレームの最後に Fence で GPU の完了を待つので、
    // Update の時点では GPU がこのバッファを使っておらず安全
    fragment.object->SetModel(nullptr);
    fragment.model.reset();

    // 平面がメッシュに当たらず、こちら側に何も残らなかった
    // （頂点 0 個ではバッファを作れないので、表示しない）
    if (data.vertices.empty()) {
      continue;
    }

    fragment.model = std::make_unique<Model>();
    fragment.model->InitializeFromModelData(
        ModelManager::GetInstance()->GetModelRenderer(), data);
    fragment.object->SetModel(fragment.model.get());
  }
}

bool FractureTestScene::DrawDebugUI() {
  bool isPlaneChanged = false;

#ifdef USE_IMGUI
  ImGui::Begin("Fracture Test");

  ImGui::SeparatorText("切断平面");
  isPlaneChanged |= ImGui::DragFloat3("法線", &planeNormal_.x, 0.01f);
  isPlaneChanged |= ImGui::DragFloat3("通る点", &planePoint_.x, 0.01f);

  ImGui::SeparatorText("表示");
  ImGui::SliderFloat("離す距離", &separation_, 0.0f, 1.0f);
  ImGui::Checkbox("元メッシュを表示", &showSource_);

  ImGui::SeparatorText("体積の検証");
  const float fragmentTotal = fragments_[0].volume + fragments_[1].volume;
  const float error = std::abs(fragmentTotal - sourceVolume_);
  ImGui::Text("元メッシュ : %.6f", sourceVolume_);
  ImGui::Text("破片A(表)  : %.6f", fragments_[0].volume);
  ImGui::Text("破片B(裏)  : %.6f", fragments_[1].volume);
  ImGui::Text("破片の合計 : %.6f", fragmentTotal);
  if (error <= kVolumeTolerance) {
    ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "誤差 %.6f  OK", error);
  } else {
    ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "誤差 %.6f  NG", error);
  }

  ImGui::End();
#endif // USE_IMGUI

  return isPlaneChanged;
}

void FractureTestScene::Draw() { Draw3D(); }

void FractureTestScene::Draw3D() {
  engine_->Begin3D();

  // ここから下で3DオブジェクトのDrawを呼ぶ
  if (showSource_) {
    sourceObject_->Draw();
  }
  for (Fragment &fragment : fragments_) {
    fragment.object->Draw();
  }
}

void FractureTestScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ
}
