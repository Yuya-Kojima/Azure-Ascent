#include "Scene/SceneManager.h"
#include "Core/EngineBase.h"
#include "Debug/Logger.h"
#include "Scene/AbstractSceneFactory.h"
#include "Scene/BaseScene.h"
#include <cassert>

//=========================
// 遷移の段階
//=========================

/// <summary>
/// 遷移の段階の基底クラス。毎フレームの処理と次の段階を決め、シーンを止めるかに答える
/// </summary>
class SceneManager::TransitionState {
public:
  explicit TransitionState(SceneManager &manager) : manager_(manager) {}
  virtual ~TransitionState() = default;

  /// <summary>
  /// この段階に入ったときの準備
  /// </summary>
  virtual void Enter() {}

  /// <summary>
  /// 毎フレームの更新。次の段階へ移るなら、その段階を返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<TransitionState> Update(float dt) = 0;

  /// <summary>
  /// 遷移の途中か
  /// </summary>
  virtual bool IsTransitioning() const { return true; }

  /// <summary>
  /// このフレーム、シーンの更新を止めるか
  /// </summary>
  virtual bool HoldsSceneUpdate() const { return false; }

protected:
  SceneManager &GetManager() const { return manager_; }

private:
  SceneManager &manager_;
};

/// <summary>
/// 遷移していない。次のシーンが予約されたら覆っていく
/// </summary>
class SceneManager::IdleState : public SceneManager::TransitionState {
public:
  using TransitionState::TransitionState;
  std::unique_ptr<TransitionState> Update(float dt) override;
  bool IsTransitioning() const override { return false; }
};

/// <summary>
/// 覆っていく。覆いきったら、覆ったまま保つ（そのときシーンを差し替える）
/// </summary>
class SceneManager::FadeOutState : public SceneManager::TransitionState {
public:
  using TransitionState::TransitionState;
  void Enter() override { GetManager().StartTransitionFade(true); }
  std::unique_ptr<TransitionState> Update(float dt) override;
};

/// <summary>
/// 覆ったまま保つ。入るときにシーンを差し替え、その時間も保つ時間に数える。
/// 差し替えたフレームは新しいシーンを 1 回更新して描画できる状態にし、その後の保つ間は更新を止める
/// </summary>
class SceneManager::HoldState : public SceneManager::TransitionState {
public:
  using TransitionState::TransitionState;
  void Enter() override { holdSec_ = GetManager().SwitchScene(); }
  std::unique_ptr<TransitionState> Update(float dt) override;
  bool HoldsSceneUpdate() const override { return isHolding_; }

private:
  float holdSec_ = 0.0f;    // 覆ってから保った秒数
  bool isHolding_ = false;  // 保つ間に入ったか（差し替えたフレームは false）
};

/// <summary>
/// 明けていく。明けきったら遷移を終える
/// </summary>
class SceneManager::FadeInState : public SceneManager::TransitionState {
public:
  using TransitionState::TransitionState;
  void Enter() override;
  std::unique_ptr<TransitionState> Update(float dt) override;
};

std::unique_ptr<SceneManager::TransitionState> SceneManager::IdleState::Update(float) {
  // 予約が入ったら覆っていく
  if (GetManager().nextScene_) {
    return std::make_unique<FadeOutState>(GetManager());
  }
  return nullptr;
}

std::unique_ptr<SceneManager::TransitionState> SceneManager::FadeOutState::Update(float) {
  // 覆いきったら、すぐシーンを差し替えて保つ
  if (GetManager().fade_.IsCovered()) {
    return std::make_unique<HoldState>(GetManager());
  }
  return nullptr;
}

std::unique_ptr<SceneManager::TransitionState> SceneManager::HoldState::Update(float dt) {
  holdSec_ += dt;
  if (holdSec_ >= GetManager().holdDurationSec_) {
    return std::make_unique<FadeInState>(GetManager());
  }
  isHolding_ = true;
  return nullptr;
}

void SceneManager::FadeInState::Enter() {
  GetManager().StartTransitionFade(false);
  GetManager().ResetTransitionSettings();
}

std::unique_ptr<SceneManager::TransitionState> SceneManager::FadeInState::Update(float) {
  // 明けきったら遷移を終える
  if (GetManager().fade_.IsClear()) {
    return std::make_unique<IdleState>(GetManager());
  }
  return nullptr;
}

//=========================
// SceneManager
//=========================

SceneManager::SceneManager() : state_(std::make_unique<IdleState>(*this)) {}

SceneManager::~SceneManager() = default;

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

  state_ = std::make_unique<IdleState>(*this);
}

void SceneManager::Finalize() {

  // 予約が残ってたら解放（予約だけのシーンは Initialize していないので、Finalize は呼ばない）
  if (nextScene_) {
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

  // 今の遷移の段階を進め、次の段階が返ってきたら入れ替える
  if (std::unique_ptr<TransitionState> next = state_->Update(dt)) {
    ChangeState(std::move(next));
  }

  //=========================
  // シーン更新
  //=========================
  if (scene_ && !state_->HoldsSceneUpdate()) {
    scene_->Update();
  }
}

void SceneManager::ChangeState(std::unique_ptr<TransitionState> next) {
  state_ = std::move(next);
  state_->Enter();
}

float SceneManager::SwitchScene() {
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

  return Ms(initializeEnd - switchStart).count() / 1000.0f;
}

void SceneManager::StartTransitionFade(bool isOut) {
  fade_.SetCloudShade(transitionCloudShadowColor_, transitionCloudGlowColor_);
  if (isOut) {
    fade_.StartFadeOut(transitionDurationSec_, transitionFadeType_,
                       transitionColor_, transitionEasing_);
  } else {
    fade_.StartFadeIn(transitionDurationSec_, transitionFadeType_,
                      transitionColor_, transitionEasing_);
  }
}

bool SceneManager::IsTransitioning() const {
  return nextScene_ != nullptr || state_->IsTransitioning();
}

void SceneManager::ResetTransitionSettings() {
  transitionDurationSec_ = 0.35f;
  transitionColor_ = Vector4{0.0f, 0.0f, 0.0f, 1.0f};
  transitionFadeType_ = Fade::FadeType::Solid;
  transitionEasing_ = Fade::EasingType::EaseInOutCubic;
  transitionCloudShadowColor_ = Fade::kDefaultCloudShadowColor;
  transitionCloudGlowColor_ = Fade::kDefaultCloudGlowColor;
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

  if (state_->IsTransitioning()) {
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

void SceneManager::SetNextTransitionCloudShade(const Vector4 &shadowColor,
                                               const Vector4 &glowColor) {
  transitionCloudShadowColor_ = shadowColor;
  transitionCloudGlowColor_ = glowColor;
}
