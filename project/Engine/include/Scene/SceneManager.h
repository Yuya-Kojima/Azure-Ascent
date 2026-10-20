#pragma once
#include "Sprite/Fade.h"
#include <chrono>
#include <memory>
#include <string>

class BaseScene;
class EngineBase;
class AbstractSceneFactory;
class PostProcess;

class SceneManager {

private:
  std::unique_ptr<BaseScene> scene_ = nullptr;
  std::string currentSceneName_ = "";

  std::unique_ptr<BaseScene> nextScene_ = nullptr;
  std::string nextSceneName_ = "";

  EngineBase *engine_ = nullptr;

  Fade fade_;

  float transitionDurationSec_ = 0.35f;
  Vector4 transitionColor_ = Vector4{0.0f, 0.0f, 0.0f, 1.0f};
  Fade::FadeType transitionFadeType_ = Fade::FadeType::Solid;
  Fade::EasingType transitionEasing_ = Fade::EasingType::EaseOutCubic;

  float holdSec_ = 0.0f;
  float holdDurationSec_ = 0.20f;

  enum class TransitionState {
    None,
    FadeOut,
    BlackHold,
    SwitchScene,
    FadeIn,
  };

  TransitionState transitionState_ = TransitionState::None;

  /// <summary>
  /// 次の遷移の設定（時間・色・種類・イージング）を既定に戻す
  /// </summary>
  void ResetTransitionSettings();

  // シーン切り替えの所要時間の計測（Debug / Development のログに出す）
  // 切り替えた後の数フレームは、1フレームにかかった実時間も出す
  static constexpr int kFramesToLogAfterSwitch_ = 8;
  int framesToLog_ = 0;
  std::chrono::steady_clock::time_point lastUpdateTime_;

public:
  static SceneManager *GetInstance();

  void Initialize(EngineBase *engine);

  void Finalize();

  void Update();

  void Draw();

  // ポストプロセスの影響を受けないUIやフェードを描画するパス
  void DrawOverlay();

  // エディタ用のUI（ImGui関連など）を描画するパス
  void DrawEditorUI();

  void ChangeScene(const std::string &sceneName);

  void SetNextTransitionFade(
      float durationSec, Fade::FadeType type = Fade::FadeType::Solid,
      const Vector4 &color = Vector4{0.0f, 0.0f, 0.0f, 1.0f},
      Fade::EasingType easing = Fade::EasingType::EaseOutCubic);

  PostProcess* GetCurrentScenePostProcess() const;

  BaseScene* GetCurrentScene() const { return scene_.get(); }
  const std::string& GetCurrentSceneName() const { return currentSceneName_; }

private:
  SceneManager() = default;
  ~SceneManager() = default;
  SceneManager(const SceneManager &) = delete;
  SceneManager &operator=(const SceneManager &) = delete;
  SceneManager(SceneManager &&) = delete;
  SceneManager &operator=(SceneManager &&) = delete;

private:
  // シーンファクトリー（借りてくる）
  AbstractSceneFactory *sceneFactory_ = nullptr;

public:
  void SetSceneFactory(AbstractSceneFactory *sceneFactory) {
    sceneFactory_ = sceneFactory;
  }
};
