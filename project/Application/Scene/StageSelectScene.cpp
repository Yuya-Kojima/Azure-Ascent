#include "StageSelectScene.h"
#include "Camera/GameCamera.h"
#include "Debug/DebugCamera.h"
#include "Framework/GameManager.h"
#include "Framework/UIManager.h"
#include "Input/InputKeyState.h"
#include "Model/ModelManager.h"
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
  camera_->SetRotate({0.3f, 0.0f, 0.0f});
  camera_->SetTranslate({0.0f, 4.0f, -10.0f});

  // デバッグカメラ
  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize({0.0f, 4.0f, -10.0f});

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
        SceneManager::GetInstance()->SetNextTransitionFade(0.5f);
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
}

void StageSelectScene::Draw() { Draw3D(); }

void StageSelectScene::Draw3D() {
  engine_->Begin3D();

  // ここから下で3DオブジェクトのDrawを呼ぶ
}

void StageSelectScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ
  UIManager::GetInstance()->Draw();
}
