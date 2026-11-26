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
  Vector4 transitionCloudShadowColor_ = Fade::kDefaultCloudShadowColor;
  Vector4 transitionCloudGlowColor_ = Fade::kDefaultCloudGlowColor;

  float holdDurationSec_ = 0.20f; // 覆ったまま保つ秒数（シーンの差し替えにかかった時間も数える）

  // 遷移の段階（SceneManager.cpp で定義する。入れ子のクラスなので、private な関数を呼べる）
  class TransitionState; // 基底
  class IdleState;       // 遷移していない（次のシーンの予約を待つ）
  class FadeOutState;    // 覆っていく
  class HoldState;       // 覆ったまま保つ（入るときにシーンを差し替える）
  class FadeInState;     // 明けていく

  std::unique_ptr<TransitionState> state_; // 今の遷移の段階

  /// <summary>
  /// 遷移の段階を next に入れ替え、入ったときの準備をさせる
  /// </summary>
  void ChangeState(std::unique_ptr<TransitionState> next);

  /// <summary>
  /// 今のシーンを終え、予約したシーンを始める。かかった秒数を返す
  /// </summary>
  float SwitchScene();

  /// <summary>
  /// 遷移のフェードを始める（isOut が true なら覆っていく、false なら明けていく）
  /// </summary>
  void StartTransitionFade(bool isOut);

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

  /// <summary>
  /// 次の遷移が雲のときの、影の色と縁の光の色（SetNextTransitionFade と一緒に呼ぶ。遷移が終わると既定に戻る）
  /// </summary>
  void SetNextTransitionCloudShade(const Vector4 &shadowColor,
                                   const Vector4 &glowColor);

  PostProcess* GetCurrentScenePostProcess() const;

  /// <summary>
  /// シーンの切り替え中か（次のシーンが決まってから、遷移のフェードが明けきるまで）
  /// </summary>
  bool IsTransitioning() const;

  BaseScene* GetCurrentScene() const { return scene_.get(); }
  const std::string& GetCurrentSceneName() const { return currentSceneName_; }

private:
  SceneManager();
  ~SceneManager();
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
