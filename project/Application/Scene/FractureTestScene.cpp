#include "FractureTestScene.h"
#include "Debug/DebugCamera.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Math/MathUtil.h"
#include "Renderer/Object3dRenderer.h"

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
} // namespace

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
}

void FractureTestScene::Finalize() {}

void FractureTestScene::Update() {

  // カメラの更新
  debugCamera_->Update(*engine_->GetInputManager());
  engine_->GetObject3dRenderer()->SetDefaultCamera(debugCamera_->GetCamera());

  sourceObject_->Update();
}

void FractureTestScene::Draw() { Draw3D(); }

void FractureTestScene::Draw3D() {
  engine_->Begin3D();

  // ここから下で3DオブジェクトのDrawを呼ぶ
  sourceObject_->Draw();
}

void FractureTestScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ
}
