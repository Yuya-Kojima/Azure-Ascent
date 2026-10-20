#include "Scene/SceneManager.h"
#include "Core/EngineBase.h"
#include "Debug/Logger.h"
#include "Scene/AbstractSceneFactory.h"
#include "Scene/BaseScene.h"
#include <cassert>

SceneManager *SceneManager::GetInstance() {
  static SceneManager instance;
  return &instance;
}

void SceneManager::Initialize(EngineBase *engine) {
  engine_ = engine;
  assert(engine_);

  SpriteRenderer *spriteRenderer = engine_->GetSpriteRenderer();
  assert(spriteRenderer);

  fade_.Initialize(spriteRenderer, "resources/white1x1.png");

  transitionState_ = TransitionState::None;
}

void SceneManager::Finalize() {

  // 予約が残ってたら解放
  if (nextScene_) {
    nextScene_->Finalize();
    nextScene_.reset();
  }

  // 現在シーン解放
  if (scene_) {
    scene_->Finalize();
    scene_.reset();
  }

  fade_.Finalize();

  engine_ = nullptr;
}

void SceneManager::Update() {

  const float dt = 1.0f / 60.0f;
  fade_.Update(dt);

  // 切り替えた後の数フレームは、前のフレームからの実時間をログに出す
  auto now = std::chrono::steady_clock::now();
  if (framesToLog_ > 0) {
    float frameMs =
        std::chrono::duration<float, std::milli>(now - lastUpdateTime_).count();
    Logger::Log("[SceneManager] frame " +
                std::to_string(kFramesToLogAfterSwitch_ - framesToLog_ + 1) +
                " after switch: " + std::to_string(frameMs) + " ms\n");
    --framesToLog_;
  }
  lastUpdateTime_ = now;

  // 予約が入ったらフェードアウト開始
  if (transitionState_ == TransitionState::None) {
    if (nextScene_) {
      transitionState_ = TransitionState::FadeOut;
      fade_.StartFadeOut(transitionDurationSec_, transitionFadeType_,
                         transitionColor_, transitionEasing_);
    }
  }

  // フェードアウト完了待ち：覆いきったらすぐシーンを切り替える
  if (transitionState_ == TransitionState::FadeOut) {
    if (fade_.IsCovered()) {
      transitionState_ = TransitionState::SwitchScene;
    }
  }

  // 覆ったまま保持：切り替えにかかった時間も保持の時間に数える
  // 保持の間は新しいシーンを更新しない（フェードインと同時に動き出す）
  bool isHolding = false;
  if (transitionState_ == TransitionState::BlackHold) {
    holdSec_ += dt;
    if (holdSec_ >= holdDurationSec_) {
      transitionState_ = TransitionState::FadeIn;
      fade_.StartFadeIn(transitionDurationSec_, transitionFadeType_,
                        transitionColor_, transitionEasing_);
      ResetTransitionSettings();
    } else {
      isHolding = true;
    }
  }

  // シーン切り替え（このフレームは新しいシーンを1回更新して、描画できる状態にする）
  if (transitionState_ == TransitionState::SwitchScene) {

    auto switchStart = std::chrono::steady_clock::now();

    // 旧シーン終了
    if (scene_) {
      scene_->Finalize();
      scene_.reset();
    }
    auto finalizeEnd = std::chrono::steady_clock::now();

    // 次シーンへ
    scene_ = std::move(nextScene_);
    currentSceneName_ = nextSceneName_;

    scene_->SetSceneManger(this);
    scene_->Initialize(engine_);

    auto initializeEnd = std::chrono::steady_clock::now();
    using Ms = std::chrono::duration<float, std::milli>;
    Logger::Log("[SceneManager] " + currentSceneName_ + " switch: finalize " +
                std::to_string(Ms(finalizeEnd - switchStart).count()) +
                " ms / initialize " +
                std::to_string(Ms(initializeEnd - finalizeEnd).count()) +
                " ms\n");
    framesToLog_ = kFramesToLogAfterSwitch_;

    // 覆ったまま保持へ（切り替えにかかった時間はすでに保持した分とする）
    transitionState_ = TransitionState::BlackHold;
    holdSec_ = Ms(initializeEnd - switchStart).count() / 1000.0f;
  }

  // フェードイン完了待ち
  if (transitionState_ == TransitionState::FadeIn) {
    if (fade_.IsClear()) {
      transitionState_ = TransitionState::None;
    }
  }

  //=========================
  // シーン更新
  //=========================
  if (scene_ && !isHolding) {
    scene_->Update();
  }
}

void SceneManager::ResetTransitionSettings() {
  transitionDurationSec_ = 0.35f;
  transitionColor_ = Vector4{0.0f, 0.0f, 0.0f, 1.0f};
  transitionFadeType_ = Fade::FadeType::Solid;
  transitionEasing_ = Fade::EasingType::EaseInOutCubic;
}

void SceneManager::Draw() {
  if (scene_) {
    scene_->Draw();
  }
}

void SceneManager::DrawOverlay() {
  assert(engine_);
  engine_->Begin2D();

  if (scene_) {
    scene_->Draw2D();
  }

  fade_.Draw();

  engine_->End2D();
}

void SceneManager::DrawEditorUI() {
  if (scene_) {
    scene_->DrawEditorUI();
  }
}

void SceneManager::ChangeScene(const std::string &sceneName) {

  assert(sceneFactory_);

  if (transitionState_ != TransitionState::None) {
    Logger::Log("SceneManager::ChangeScene ignored: transitioning\n");
    return;
  }

  if (nextScene_) {
    Logger::Log("SceneManager::ChangeScene ignored: nextScene already set\n");
    return;
  }

  // 次シーンを生成
  nextScene_ = sceneFactory_->CreateScene(sceneName);
  nextSceneName_ = sceneName;
}

PostProcess* SceneManager::GetCurrentScenePostProcess() const {
  if (scene_) {
    return scene_->GetPostProcess();
  }
  return nullptr;
}

void SceneManager::SetNextTransitionFade(float durationSec, Fade::FadeType type,
                                         const Vector4 &color,
                                         Fade::EasingType easing) {
  transitionDurationSec_ = (std::max)(durationSec, 0.0001f);
  transitionFadeType_ = type;
  transitionColor_ = color;
  transitionEasing_ = easing;
}
