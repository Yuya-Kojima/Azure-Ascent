#include "FractureTestScene.h"
#include "Debug/DebugCamera.h"
#include "Effect/FracturePieceSet.h"
#include "Effect/MeshFracture.h"
#include "Math/MathUtil.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Renderer/Object3dRenderer.h"
#include <algorithm>
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

// ボロノイの種の数（初期値と、スライダーの範囲）
// 上限は計算量 O(種の数^2 x 三角形数)
// と見やすさから、実際の演出で使う程度に抑える
const int kDefaultSeedCount = 8;
const int kMinSeedCount = 1;
const int kMaxSeedCount = 32;

// ボロノイの種を置く乱数のシードの初期値
const int kDefaultRandomSeed = 1;

// 種の偏らせ方のスライダーの範囲
// 集中度 1/3 が球の中に均等（3次元では半径 r の殻の広さが r^2 に比例するため）
const float kMinSeedBiasRadius = 0.05f;
const float kMaxSeedBiasRadius = 1.0f;
const float kMinSeedBiasFalloff = 1.0f / 3.0f;
const float kMaxSeedBiasFalloff = 4.0f;
const float kMaxSeedBiasMinSpacing = 0.2f;

// 平均に対してこれより小さい破片は、画面ではほぼ見えないとみなす
const float kTinyPieceRatio = 0.01f;

// 衝撃点の目印の大きさ（元メッシュの一辺 1 に対して小さく）
const float kImpactMarkerScale = 0.05f;

// 体積の誤差の許容値（float の丸め誤差程度なら一致とみなす）
const float kVolumeTolerance = 1e-4f;

// 爆発させる敵の大きさの範囲（スライダー用）
const float kMinDebrisScale = 0.1f;
const float kMaxDebrisScale = 5.0f;
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

  // 元メッシュ（ModelManager
  // の共有データはそのまま残し、コピーのテクスチャだけ変える）
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

  // 衝撃点の目印（元メッシュを小さくして使う）
  impactMarker_ = std::make_unique<Object3d>();
  impactMarker_->Initialize(engine_->GetObject3dRenderer());
  impactMarker_->SetModel(sourceModel_.get());
  impactMarker_->SetScale(
      {kImpactMarkerScale, kImpactMarkerScale, kImpactMarkerScale});

  // 破片
  planeNormal_ = kDefaultPlaneNormal;
  separation_ = kDefaultSeparation;
  seedCount_ = kDefaultSeedCount;
  randomSeed_ = kDefaultRandomSeed;
  pieceSet_ = std::make_unique<FracturePieceSet>();
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

  if (debris_) {
    // 爆発中（ゲームでは ActorManager がやる Update と、死んだら消す処理）
    debris_->Update();
    if (debris_->IsDead()) {
      debris_.reset();
    }
  } else {
    // 止まっているときは、破片を切り口から離して置く
    for (Fragment &fragment : fragments_) {
      fragment.object->SetTranslation(
          fragment.basePosition + fragment.separationDirection * separation_);
      fragment.object->SetRotation(fragmentRotation_);
      fragment.object->Update();
    }
  }

  sourceObject_->Update();
  impactMarker_->SetTranslation(impactPoint_);
  impactMarker_->Update();
}

void FractureTestScene::RebuildFragments() {

  // 形セットを作り直す前に、それを借りている爆発を先に消す
  debris_.reset();

  // 古い GPU バッファはここで解放される
  // このエンジンは毎フレームの最後に Fence で GPU の完了を待つので、
  // Update の時点では GPU がこのバッファを使っておらず安全
  fragments_.clear();

  const Model::ModelData &source = sourceModel_->GetModelData();
  switch (mode_) {
  case Mode::kPlane:
    BuildPlaneFragments(source);
    break;
  case Mode::kVoronoi:
    BuildVoronoiFragments(source);
    break;
  }

  // 爆発用の形セット（平面モードでも、今のボロノイの設定で作る）
  pieceSet_->Create(source, static_cast<uint32_t>(seedCount_),
                    static_cast<uint32_t>(randomSeed_), seedBias_);
}

void FractureTestScene::BuildPlaneFragments(const Model::ModelData &source) {

  // 法線の長さが 0 だと平面にならないので、作らない
  if (LengthSq(SafeNormalize(planeNormal_)) == 0.0f) {
    return;
  }

  // 表側の破片は平面そのまま、裏側の破片は裏返した平面で切る
  const MeshFracture::Plane plane =
      MeshFracture::MakePlane(planeNormal_, planePoint_);
  for (const MeshFracture::Plane &p : {plane, MeshFracture::FlipPlane(plane)}) {
    AddFragment(MeshFracture::ClipMesh(source, p), p.normal);
  }
}

void FractureTestScene::BuildVoronoiFragments(const Model::ModelData &source) {

  // 元メッシュを囲む箱の中に種をばらまく
  Vector3 sourceMin, sourceMax;
  MeshFracture::ComputeBounds(source, sourceMin, sourceMax);
  const std::vector<Vector3> seeds = MeshFracture::GenerateSeeds(
      sourceMin, sourceMax, static_cast<uint32_t>(seedCount_),
      static_cast<uint32_t>(randomSeed_), seedBias_, &impactPoint_);

  // 破片は「元メッシュの重心 → 破片の重心」の向きに離す（爆発したように見える）
  const Vector3 sourceCenter = MeshFracture::ComputeCentroid(source);
  for (const Model::ModelData &piece :
       MeshFracture::VoronoiFracture(source, seeds)) {
    const Vector3 pieceCenter = MeshFracture::ComputeCentroid(piece);
    AddFragment(piece, SafeNormalize(pieceCenter - sourceCenter));
  }
}

void FractureTestScene::AddFragment(const Model::ModelData &data,
                                    const Vector3 &separationDirection) {

  // 何も残らなかった破片は作らない（頂点 0 個ではバッファを作れない）
  if (data.vertices.empty()) {
    return;
  }

  Fragment fragment;
  fragment.volume = MeshFracture::ComputeVolume(data);
  fragment.separationDirection = separationDirection;

  // 原点を重心に移す。ずらした分は basePosition に置いて見た目を元に戻す
  Model::ModelData meshData = data;
  if (centerPivot_) {
    fragment.basePosition = MeshFracture::CenterMesh(meshData);
  }

  fragment.model = std::make_unique<Model>();
  fragment.model->InitializeFromModelData(
      ModelManager::GetInstance()->GetModelRenderer(), meshData);

  fragment.object = std::make_unique<Object3d>();
  fragment.object->Initialize(engine_->GetObject3dRenderer());
  fragment.object->SetModel(fragment.model.get());

  fragments_.push_back(std::move(fragment));
}

void FractureTestScene::Explode() {

  // 前の爆発が残っていれば、新しい爆発で置き換える
  // テストシーンは原点に置いた Cube を、指定した大きさの敵とみなして爆発させる
  const Vector3 position = {0.0f, 0.0f, 0.0f};
  const Vector3 scale = {debrisScale_, debrisScale_, debrisScale_};
  const Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // テクスチャの色のまま
  debris_ = std::make_unique<FractureDebris>(
      engine_->GetObject3dRenderer(), *pieceSet_, position, scale, color,
      inheritedVelocity_, debrisParams_);
}

bool FractureTestScene::DrawDebugUI() {
  bool needsRebuild = false;

#ifdef USE_IMGUI
  ImGui::Begin("Fracture Test");

  ImGui::SeparatorText("切り方");
  int mode = static_cast<int>(mode_);
  needsRebuild |=
      ImGui::RadioButton("平面", &mode, static_cast<int>(Mode::kPlane));
  ImGui::SameLine();
  needsRebuild |=
      ImGui::RadioButton("ボロノイ", &mode, static_cast<int>(Mode::kVoronoi));
  mode_ = static_cast<Mode>(mode);

  if (mode_ == Mode::kPlane) {
    needsRebuild |= ImGui::DragFloat3("法線", &planeNormal_.x, 0.01f);
    needsRebuild |= ImGui::DragFloat3("通る点", &planePoint_.x, 0.01f);
  } else {
    needsRebuild |=
        ImGui::SliderInt("種の数", &seedCount_, kMinSeedCount, kMaxSeedCount);
    needsRebuild |= ImGui::InputInt("乱数シード", &randomSeed_);
    needsRebuild |= ImGui::Checkbox("種を衝撃点に偏らせる", &seedBias_.enabled);
    if (seedBias_.enabled) {
      needsRebuild |=
          ImGui::SliderFloat("衝撃点に寄せる割合", &seedBias_.ratio, 0.0f, 1.0f);
      needsRebuild |= ImGui::SliderFloat("衝撃点からの範囲", &seedBias_.radius,
                                         kMinSeedBiasRadius, kMaxSeedBiasRadius);
      needsRebuild |= ImGui::SliderFloat("集中度", &seedBias_.falloff,
                                         kMinSeedBiasFalloff, kMaxSeedBiasFalloff);
      needsRebuild |=
          ImGui::SliderFloat("種同士の最小の間隔", &seedBias_.minSpacing, 0.0f,
                             kMaxSeedBiasMinSpacing);
      ImGui::Text("衝撃点 : (%.2f, %.2f, %.2f)", impactPoint_.x, impactPoint_.y,
                  impactPoint_.z);
    }
  }

  ImGui::SeparatorText("表示");
  ImGui::SliderFloat("離す距離", &separation_, 0.0f, 1.0f);
  ImGui::Checkbox("元メッシュを表示", &showSource_);

  ImGui::SeparatorText("回転の中心");
  needsRebuild |= ImGui::Checkbox("原点を重心に移す", &centerPivot_);
  ImGui::DragFloat3("破片の回転", &fragmentRotation_.x, 0.01f);
  if (ImGui::Button("回転を戻す")) {
    fragmentRotation_ = {0.0f, 0.0f, 0.0f};
  }

  ImGui::SeparatorText("飛ばす（FractureDebris）");
  if (ImGui::Button("爆発")) {
    Explode();
  }
  ImGui::SameLine();
  if (ImGui::Button("リセット")) {
    debris_.reset();
  }
  ImGui::Text(debris_ ? "爆発中" : "停止中");
  ImGui::DragFloat("外向きの速さ", &debrisParams_.explosionSpeed, 0.05f, 0.0f,
                   20.0f);
  ImGui::DragFloat3("引き継ぐ速度", &inheritedVelocity_.x, 0.05f);
  ImGui::DragFloat3("重力", &debrisParams_.gravity.x, 0.05f);
  ImGui::DragFloat("角速度の最大", &debrisParams_.maxAngularSpeed, 0.05f, 0.0f,
                   30.0f);
  ImGui::DragFloat("寿命（秒）", &debrisParams_.lifetime, 0.05f, 0.1f, 10.0f);
  ImGui::DragFloat("空気抵抗", &debrisParams_.drag, 0.05f, 0.0f, 30.0f);
  ImGui::SliderFloat("敵の大きさ", &debrisScale_, kMinDebrisScale,
                     kMaxDebrisScale);

  ImGui::SeparatorText("体積の検証");
  float fragmentTotal = 0.0f;
  for (const Fragment &fragment : fragments_) {
    fragmentTotal += fragment.volume;
  }
  const float error = std::abs(fragmentTotal - sourceVolume_);
  ImGui::Text("元メッシュ : %.6f", sourceVolume_);
  ImGui::Text("破片の数   : %zu", fragments_.size());
  ImGui::Text("破片の合計 : %.6f", fragmentTotal);
  if (error <= kVolumeTolerance) {
    ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "誤差 %.6f  OK", error);
  } else {
    ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "誤差 %.6f  NG", error);
  }

  // 破片の大小の差（偏らせる前後で比べる）
  if (!fragments_.empty()) {
    float minVolume = fragments_[0].volume;
    float maxVolume = fragments_[0].volume;
    for (const Fragment &fragment : fragments_) {
      minVolume = (std::min)(minVolume, fragment.volume);
      maxVolume = (std::max)(maxVolume, fragment.volume);
    }
    ImGui::Text("最大の破片 : %.6f", maxVolume);
    ImGui::Text("最小の破片 : %.6f", minVolume);
    if (minVolume > 0.0f) {
      ImGui::Text("大小の差   : %.1f 倍", maxVolume / minVolume);
    }
    const float tinyVolume =
        sourceVolume_ / static_cast<float>(fragments_.size()) * kTinyPieceRatio;
    const auto tinyCount = std::count_if(
        fragments_.begin(), fragments_.end(),
        [&](const Fragment &f) { return f.volume < tinyVolume; });
    ImGui::Text("小さすぎる破片（平均の1%%未満）: %d 個",
                static_cast<int>(tinyCount));
  }

  if (ImGui::TreeNode("破片ごとの体積")) {
    for (size_t i = 0; i < fragments_.size(); ++i) {
      ImGui::Text("[%2zu] %.6f", i, fragments_[i].volume);
    }
    ImGui::TreePop();
  }

  ImGui::End();
#endif // USE_IMGUI

  return needsRebuild;
}

void FractureTestScene::Draw() { Draw3D(); }

void FractureTestScene::Draw3D() {
  engine_->Begin3D();

  // ここから下で3DオブジェクトのDrawを呼ぶ
  if (showSource_) {
    sourceObject_->Draw();
  }
  // 目印は止まった破片の確認用なので、爆発中は消す
  if (mode_ == Mode::kVoronoi && seedBias_.enabled && !debris_) {
    impactMarker_->Draw();
  }
  if (debris_) {
    // 爆発中は、止まった破片の代わりに爆発を描く
    debris_->Draw3D();
  } else {
    for (Fragment &fragment : fragments_) {
      fragment.object->Draw();
    }
  }
}

void FractureTestScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ
}
