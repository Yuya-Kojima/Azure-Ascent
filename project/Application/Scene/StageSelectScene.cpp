#include "StageSelectScene.h"
#include "Camera/GameCamera.h"
#include "Debug/DebugCamera.h"
#include "Framework/GameManager.h"
#include "Framework/UIManager.h"
#include "Input/InputKeyState.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Render/SkyBox/SkyBox.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/SpriteRenderer.h"
#include "Scene/SceneManager.h"
#include "Texture/TextureManager.h"

#ifdef USE_IMGUI
#include "Debug/ImGuiManager.h"
#endif

void StageSelectScene::Initialize(EngineBase *engine) {

  // 基底クラスの初期化（PostProcessの生成など）
  BaseScene::Initialize(engine);

  // 参照をコピー
  engine_ = engine;

  // カメラの生成と初期化
  camera_ = std::make_unique<GameCamera>();

  // デバッグカメラ
  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize(cameraPos_);

  //===========================
  // 拠点（地上から空を見上げる）
  //===========================
  // 空：タイトルと同じ手続き生成の空
  TextureManager::GetInstance()->LoadTexture("resources/Skybox/Skybox.dds");
  skybox_ = std::make_unique<Skybox>();
  skybox_->Initialize(engine_->GetSkyboxRenderer());
  skybox_->SetTexture("resources/Skybox/Skybox.dds");
  skybox_->SetScale({100.0f, 100.0f, 100.0f});
  skybox_->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
  skybox_->SetProceduralSky(true);

  ModelManager::GetInstance()->LoadModel("terrain.obj");
  ModelManager::GetInstance()->LoadModel("player_dragon.obj");

  // 地面
  groundObject_ = std::make_unique<Object3d>();
  groundObject_->Initialize(engine_->GetObject3dRenderer());
  groundObject_->SetModel("terrain.obj");
  // 地面を大きく引き伸ばすと草のテクスチャがぼやけるので、繰り返し貼って細かさを保つ
  const float kGroundTextureRepeat = 40.0f;
  groundObject_->SetUVTransform(
      MakeScaleMatrix({kGroundTextureRepeat, kGroundTextureRepeat, 1.0f}));

  // ドラゴン（タイトルと同じく淡い水色のリムライトで輪郭を見せる）
  dragonObject_ = std::make_unique<Object3d>();
  dragonObject_->Initialize(engine_->GetObject3dRenderer());
  dragonObject_->SetModel("player_dragon.obj");
  dragonObject_->SetRimLight({0.15f, 0.55f, 1.0f}, 1.2f, 2.5f);

  ApplyBaseLayout();

  // デフォルトカメラのセット
  engine_->GetObject3dRenderer()->SetDefaultCamera(camera_.get());

  // UIのクリアとこのシーン用のUIの準備
  UIManager::GetInstance()->Load("resources/UI/StageSelectUI.json");

  // オーディオ読み込み
  SoundManager::GetInstance()->Load("ui_decide",
                                    "resources/Sounds/ui_decide.wav");
  SoundManager::GetInstance()->Load("title_bgm",
                                    "resources/Sounds/title_bgm.mp3");
  SoundManager::GetInstance()->PlayBGM("title_bgm");
}

void StageSelectScene::Finalize() {}

void StageSelectScene::Update() {

  // Sound更新
  SoundManager::GetInstance()->Update();

  // スプライト（UI）の更新
  Input *input = GameManager::GetInstance()->IsGlobalPlayMode()
                     ? engine_->GetInputManager()
                     : nullptr;
  UIManager::GetInstance()->Update(input);

  // デバッグカメラ切り替え
  if (engine_->GetInputManager()->IsTriggerKey(DIK_P)) {
    if (useDebugCamera_) {
      useDebugCamera_ = false;
    } else {
      useDebugCamera_ = true;
    }
  }

  // 決定キー（エンターキーまたはパッドのAボタン）の処理
  if (GameManager::GetInstance()->IsGlobalPlayMode()) {
    if (engine_->GetInputManager()->IsTriggerKey(DIK_RETURN) ||
        engine_->GetInputManager()->IsPadTrigger(PadButton::A)) {
      SoundManager::GetInstance()->PlaySE("ui_decide");
      std::string selectedName = UIManager::GetInstance()->GetFocusedNodeName();

      // ステージへ出撃するときの遷移：雲を突き抜けて雲の上へ出る演出
      // 雲の量の増え方はノイズの分布で緩急が付くので、イージングは Linear にする
      const float kCloudTransitionSec = 1.0f;
      const Vector4 kCloudColor = {1.0f, 1.0f, 1.0f, 1.0f};

      if (selectedName == "Stage1Text") {
        GameManager::GetInstance()->SetCurrentLevel("level_editor.json");
        SceneManager::GetInstance()->SetNextTransitionFade(
            kCloudTransitionSec, Fade::FadeType::Cloud, kCloudColor,
            Fade::EasingType::Linear);
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
      } else if (selectedName == "Stage2Text") {
        GameManager::GetInstance()->SetCurrentLevel(
            "level_editor.json"); // とりあえず同じ
        SceneManager::GetInstance()->SetNextTransitionFade(
            kCloudTransitionSec, Fade::FadeType::Cloud, kCloudColor,
            Fade::EasingType::Linear);
        SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
      } else if (selectedName == "BackText") {
        // タイトルも雲の上にあるので、出撃と同じく雲を突き抜けて上昇する
        SceneManager::GetInstance()->SetNextTransitionFade(
            kCloudTransitionSec, Fade::FadeType::Cloud, kCloudColor,
            Fade::EasingType::Linear);
        SceneManager::GetInstance()->ChangeScene("TITLE");
      }
    }
  }

  //=======================
  // カメラの更新
  //=======================
  const ICamera *activeCamera = nullptr;

  if (useDebugCamera_) {
    debugCamera_->Update(*engine_->GetInputManager());
    activeCamera = debugCamera_->GetCamera();
  } else {
    camera_->Update();
    activeCamera = camera_.get();
  }

  // アクティブカメラを描画で使用する
  engine_->GetObject3dRenderer()->SetDefaultCamera(activeCamera);

  //=======================
  // 拠点の更新
  //=======================
  UpdateSky(activeCamera);
  ApplyBaseLayout();
  groundObject_->Update();
  dragonObject_->Update();
}

void StageSelectScene::UpdateSky(const ICamera *activeCamera) {
  // タイトルの「昼」と同じ空。頭上に雲がかかるよう雲量はタイトルより多め
  const Vector3 kZenith = {0.04f, 0.24f, 0.74f};
  const Vector3 kHorizon = {0.36f, 0.62f, 0.96f};
  const Vector3 kSunDir = {-0.35f, 0.62f, 0.70f};
  const Vector3 kSunColor = {1.00f, 0.97f, 0.90f};
  const float kSunIntensity = 0.70f;
  const float kCloudAmount = 0.6f;
  const float kSkyTimeStep = 1.0f / 60.0f;

  skyTimer_ += kSkyTimeStep;
  skybox_->SetCamera(activeCamera);
  skybox_->SetSkyColors(kZenith, kHorizon);
  skybox_->SetSun(Normalize(kSunDir), kSunColor, kSunIntensity, kCloudAmount);
  skybox_->SetSkyTime(skyTimer_);
  skybox_->Update();

  // ライト：ライトとフォグは全シーン共通の設定なので、このシーンの値で毎フレーム上書きする
  if (auto *dl = engine_->GetObject3dRenderer()->GetDirectionalLightData()) {
    dl->direction = Normalize({0.55f, -0.45f, 0.65f});
    dl->color = {1.0f, 1.0f, 1.0f, 1.0f};
    dl->intensity = 1.05f;
  }

  // フォグ：地面の端を地平線の色に溶かして、空との境目を消す
  FogData fog;
  fog.color = Vector4(kHorizon.x, kHorizon.y, kHorizon.z, 1.0f);
  fog.nearDist = 40.0f;
  fog.farDist = 300.0f;
  fog.enabled = 1.0f;
  engine_->GetObject3dRenderer()->SetFog(fog);
}

void StageSelectScene::ApplyBaseLayout() {
  camera_->SetTranslate(cameraPos_);
  camera_->SetRotate(cameraRot_);

  groundObject_->SetTranslation(groundPos_);
  groundObject_->SetScale(groundScale_);

  dragonObject_->SetTranslation(dragonPos_);
  dragonObject_->SetRotation(dragonRot_);
  dragonObject_->SetScale({dragonScale_, dragonScale_, dragonScale_});
}

void StageSelectScene::Draw() { Draw3D(); }

void StageSelectScene::Draw3D() {
  engine_->Begin3D();

  // 空
  engine_->GetSkyboxRenderer()->Begin();
  skybox_->Draw();

  // 通常の3Dオブジェクト
  engine_->GetObject3dRenderer()->Begin();
  groundObject_->Draw();
  dragonObject_->Draw();
}

void StageSelectScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ
  UIManager::GetInstance()->Draw();
}

void StageSelectScene::DrawEditorUI() {
#ifdef USE_IMGUI
  ImGui::Begin("Stage Select Base");
  ImGui::DragFloat3("Camera Pos", &cameraPos_.x, 0.05f);
  ImGui::DragFloat3("Camera Rot", &cameraRot_.x, 0.005f);
  ImGui::DragFloat3("Dragon Pos", &dragonPos_.x, 0.05f);
  ImGui::DragFloat3("Dragon Rot", &dragonRot_.x, 0.01f);
  ImGui::DragFloat("Dragon Scale", &dragonScale_, 0.01f);
  ImGui::DragFloat3("Ground Pos", &groundPos_.x, 0.1f);
  ImGui::DragFloat3("Ground Scale", &groundScale_.x, 0.1f);
  ImGui::End();
#endif
}
