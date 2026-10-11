#pragma once
#include "Math/MathUtil.h"
#include <memory>
#include <string>

class Sprite;
class SpriteRenderer;

class Fade {

public:
  enum class FadeType {
    Solid,
    WipeLeft,
    WipeRight,
    WipeUp,
    WipeDown,
    Cloud, // ノイズ＋しきい値で雲が上から覆い、上から晴れる（雲を突き抜けて上昇する演出）
  };

  enum class EasingType {
    Linear,
    EaseOutCubic,
    EaseInOutCubic,
    EaseOutQuint,
  };

  void Initialize(SpriteRenderer *spriteRenderer,
                  const std::string &texturePath = "resources/white1x1.png");

  void Finalize();

  void Update(float deltaTime);

  void Draw();

  // デフォルトフェード
  void StartFadeOut(float durationSec);
  void StartFadeIn(float durationSec);

  // タイプ、色指定、イージング
  void StartFadeOut(float durationSec, FadeType type, const Vector4 &color,
                    EasingType easing);

  void StartFadeIn(float durationSec, FadeType type, const Vector4 &color,
                   EasingType easing);

  bool IsPlaying() const { return state_ != State::Idle; }
  bool IsFinished() const { return state_ == State::Idle; }

  bool IsFading() const { return state_ != State::Idle; }
  bool IsCovered() const { return (state_ == State::Idle) && (alpha_ >= 1.0f); }
  bool IsClear() const { return (state_ == State::Idle) && (alpha_ <= 0.0f); }

  Fade();
  ~Fade();

private:
  enum class State {
    Idle,
    FadingOut,
    FadingIn,
  };

  FadeType type_ = FadeType::Solid;
  EasingType easing_ = EasingType::EaseOutCubic;

  void ApplySolid_(float eased);
  void ApplyWipeLeft_(float eased);
  void ApplyWipeRight_(float eased);
  void ApplyWipeUp_(float eased);
  void ApplyWipeDown_(float eased);
  void ApplyCloud_(float eased);
  void ApplyToSprite_();
  static float ApplyEasing_(float t, EasingType easing);
  void ApplyByType_(float eased);

  SpriteRenderer *spriteRenderer_ = nullptr;
  std::unique_ptr<Sprite> sprite_ = nullptr;

  // 雲遷移用の全画面スプライト（ノイズテクスチャを UIEffect で描く）
  std::unique_ptr<Sprite> cloudSprite_ = nullptr;
  // 雲の形を決めるノイズテクスチャ
  static inline const std::string kCloudTexturePath_ = "resources/noise0.png";
  // UIEffect.PS.hlsl の effectType（2 = 雲ディゾルブ）
  static constexpr int kCloudEffectType_ = 2;
  // 縁を光らせる幅（しきい値と比べる値 0〜1 のうち、雲の内側のどれだけを縁とするか）
  float cloudEdgeWidth_ = 0.1f;
  // 縁の光の強さ。雲に突っ込むときは控えめ、雲の上に抜けるときは強く
  float cloudGlowCovering_ = 0.4f;
  float cloudGlowClearing_ = 1.0f;
  // 雲を流すための経過時間（秒）。雲が見えている間だけ進める
  float cloudTime_ = 0.0f;

  std::string texturePath_;

  State state_ = State::Idle;

  Vector4 color_{};

  float alpha_ = 0.0f;
  float speed_ = 1.0f;
  float target_ = 0.0f;
};
