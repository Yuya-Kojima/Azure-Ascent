#include "StageSelectScene.h"
#include "Actor/DragonModel.h"
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

StageSelectScene::StageSelectScene() = default;
StageSelectScene::~StageSelectScene() = default;

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

  // 地面
  groundObject_ = std::make_unique<Object3d>();
  groundObject_->Initialize(engine_->GetObject3dRenderer());
  groundObject_->SetModel("terrain.obj");
  // 地面を大きく引き伸ばすと草のテクスチャがぼやけるので、繰り返し貼って細かさを保つ
  const float kGroundTextureRepeat = 40.0f;
  groundObject_->SetUVTransform(
      MakeScaleMatrix({kGroundTextureRepeat, kGroundTextureRepeat, 1.0f}));

  // ドラゴン（羽ばたくボーン付きモデル。リムライトは DragonModel で設定）
  dragonModel_ = std::make_unique<DragonModel>();
  dragonModel_->Initialize(engine_->GetObject3dRenderer(),
                           engine_->GetSrvManager());
  dragonObject_ = dragonModel_->GetObject3d();
  dragonModel_->SetWingStateImmediate(idleWing_);

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

  // スプライト（UI）の更新。出撃演出中はメニューを操作させない
  Input *input =
      GameManager::GetInstance()->IsGlobalPlayMode() && !isLaunching_
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
  if (GameManager::GetInstance()->IsGlobalPlayMode() && !isLaunching_) {
    if (engine_->GetInputManager()->IsTriggerKey(DIK_RETURN) ||
        engine_->GetInputManager()->IsPadTrigger(PadButton::A)) {
      SoundManager::GetInstance()->PlaySE("ui_decide");
      std::string selectedName = UIManager::GetInstance()->GetFocusedNodeName();

      // 雲を突き抜ける遷移
      // 雲の量の増え方はノイズの分布で緩急が付くので、イージングは Linear にする
      const float kCloudTransitionSec = 1.0f;
      const Vector4 kCloudColor = {1.0f, 1.0f, 1.0f, 1.0f};

      // ステージへの出撃は、ドラゴンが飛び立つ演出のあとで雲のフェードに入る
      if (selectedName == "Stage1Text") {
        GameManager::GetInstance()->SetCurrentLevel("level_editor.json");
        StartLaunch();
      } else if (selectedName == "Stage2Text") {
        GameManager::GetInstance()->SetCurrentLevel(
            "level_editor.json"); // とりあえず同じ
        StartLaunch();
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
  // 拠点の配置と出撃演出
  //=======================
  ApplyBaseLayout();
  if (isLaunching_) {
    UpdateLaunch();
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
  groundObject_->Update();

  // 羽ばたき：待機 → かがむ → 飛び立ち
  if (!isLaunching_) {
    dragonModel_->SetWingState(idleWing_);
  } else if (launchTimer_ < crouchSec_ + crouchHoldSec_) {
    dragonModel_->SetWingState(crouchWing_);
  } else {
    dragonModel_->SetWingState(leapWing_);
  }
  dragonModel_->Update(kWingDeltaTime_);
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

void StageSelectScene::StartLaunch() {
  isLaunching_ = true;
  launchTimer_ = 0.0f;
  hasRequestedSceneChange_ = false;
}

void StageSelectScene::UpdateLaunch() {
  const float kFrameSec = 1.0f / 60.0f;
  launchTimer_ += kFrameSec;

  // メニュー：薄くして消す
  float menuFade = (std::min)(launchTimer_ / menuFadeSec_, 1.0f);
  UIManager::GetInstance()->SetGlobalAlpha(1.0f - menuFade);

  //===========================
  // S1 かがむ：沈み込みながら機首を下げ、少し止まってためる
  // カメラはその間にドラゴンへ少し寄る
  //===========================
  float crouchEase = EaseOutQuad((std::min)(launchTimer_ / crouchSec_, 1.0f));
  Vector3 crouchedPos = dragonPos_ - Vector3{0.0f, crouchDepth_, 0.0f};
  Vector3 dragonPos = Lerp(dragonPos_, crouchedPos, crouchEase);
  Vector3 dragonRot = dragonRot_;
  dragonRot.x += crouchPitch_ * crouchEase;

  float crouchTotalSec = crouchSec_ + crouchHoldSec_;
  float dollyEase =
      EaseInOutQuad((std::min)(launchTimer_ / crouchTotalSec, 1.0f));
  Vector3 toDragon = Normalize(dragonPos_ - cameraPos_);
  Vector3 cameraPos = cameraPos_ + toDragon * (dollyDistance_ * dollyEase);
  Vector3 cameraRot = cameraRot_;

  float flightTime = launchTimer_ - crouchTotalSec;
  if (flightTime > 0.0f) {
    //===========================
    // S2 飛び立ち
    // 上昇量 h(t) = vc*t + (v0 - vc)*τ*(1 - e^(-t/τ))
    // 速度は跳ねた瞬間の v0 から、時定数 τ で一定の速度 vc へなめらかに落ちる
    //===========================
    float height =
        cruiseSpeed_ * flightTime +
        (leapSpeed_ - cruiseSpeed_) * leapDecaySec_ *
            (1.0f - std::exp(-flightTime / leapDecaySec_));
    Vector3 heading = {std::sin(dragonRot_.y), 0.0f, std::cos(dragonRot_.y)};
    dragonPos = crouchedPos + Vector3{0.0f, height, 0.0f} +
                heading * (height * forwardRatio_);

    // 機首：かがんだ角度から上昇の角度へ
    float pitchEase =
        EaseOutCubic((std::min)(flightTime / pitchUpSec_, 1.0f));
    dragonRot.x =
        Lerp(dragonRot_.x + crouchPitch_, flightPitch_, pitchEase);

    // カメラ：ドラゴンの移動量の一部だけついて行き（クレーン）、ドラゴンより少し下を見る
    cameraPos += (dragonPos - crouchedPos) * craneFollow_;
    float turnEase =
        EaseInOutQuad((std::min)(flightTime / cameraTurnSec_, 1.0f));
    Vector3 lookTarget =
        dragonPos - Vector3{0.0f, lookBelowDragon_ * turnEase, 0.0f};
    Vector3 lookRot = LookRotation(cameraPos, lookTarget);
    cameraRot.x = LerpAngle(cameraRot_.x, lookRot.x, turnEase);
    cameraRot.y = LerpAngle(cameraRot_.y, lookRot.y, turnEase);

    // 跳ねた瞬間の揺れ：2つの周波数のsinでずらし、残り時間の2乗で弱める
    float shakeRemain =
        1.0f - (std::min)(flightTime / leapShakeSec_, 1.0f);
    float shakePower = leapShake_ * shakeRemain * shakeRemain;
    cameraPos += Vector3{std::sin(flightTime * 53.0f),
                         std::sin(flightTime * 71.0f + 1.3f), 0.0f} *
                 shakePower;

    //===========================
    // S3 雲のフェード：雲が画面の上から覆う
    //===========================
    if (!hasRequestedSceneChange_ && flightTime >= flightSec_) {
      const Vector4 kCloudColor = {1.0f, 1.0f, 1.0f, 1.0f};
      SceneManager::GetInstance()->SetNextTransitionFade(
          cloudFadeSec_, Fade::FadeType::Cloud, kCloudColor,
          Fade::EasingType::Linear);
      SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
      hasRequestedSceneChange_ = true;
    }
  }

  dragonObject_->SetTranslation(dragonPos);
  dragonObject_->SetRotation(dragonRot);
  camera_->SetTranslate(cameraPos);
  camera_->SetRotate(cameraRot);
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
  dragonModel_->Draw();
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
  ImGui::DragFloat4("Idle Wing", &idleWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::DragFloat4("Crouch Wing", &crouchWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::DragFloat4("Leap Wing", &leapWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::SliderFloat("Leap Wing Pose", &leapWing_.pose, 0.0f, 1.0f);
  ImGui::SliderFloat("Leap Wing Hover", &leapWing_.hover, 0.0f, 1.0f);
  ImGui::DragFloat3("Ground Pos", &groundPos_.x, 0.1f);
  ImGui::DragFloat3("Ground Scale", &groundScale_.x, 0.1f);

  ImGui::SeparatorText("Launch");
  if (ImGui::Button("Reset Launch")) {
    isLaunching_ = false;
    UIManager::GetInstance()->SetGlobalAlpha(1.0f);
  }
  ImGui::Text("Time %.2f", launchTimer_);
  ImGui::DragFloat("Menu Fade Sec", &menuFadeSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Crouch Sec", &crouchSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Crouch Hold Sec", &crouchHoldSec_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Crouch Depth", &crouchDepth_, 0.01f);
  ImGui::DragFloat("Crouch Pitch", &crouchPitch_, 0.01f);
  ImGui::DragFloat("Dolly Distance", &dollyDistance_, 0.05f);
  ImGui::DragFloat("Flight Sec", &flightSec_, 0.05f, 0.1f, 5.0f);
  ImGui::DragFloat("Leap Speed", &leapSpeed_, 0.1f);
  ImGui::DragFloat("Cruise Speed", &cruiseSpeed_, 0.1f);
  ImGui::DragFloat("Leap Decay Sec", &leapDecaySec_, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("Forward Ratio", &forwardRatio_, 0.01f);
  ImGui::SliderFloat("Flight Pitch", &flightPitch_, -1.5f, 0.0f);
  ImGui::DragFloat("Pitch Up Sec", &pitchUpSec_, 0.01f, 0.01f, 2.0f);
  ImGui::SliderFloat("Crane Follow", &craneFollow_, 0.0f, 1.0f);
  ImGui::DragFloat("Camera Turn Sec", &cameraTurnSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Look Below Dragon", &lookBelowDragon_, 0.05f);
  ImGui::DragFloat("Leap Shake", &leapShake_, 0.01f);
  ImGui::DragFloat("Leap Shake Sec", &leapShakeSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Cloud Fade Sec", &cloudFadeSec_, 0.05f, 0.1f, 3.0f);
  ImGui::End();
#endif
}
