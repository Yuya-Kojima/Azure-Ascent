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
    CloudDescend, // Cloud の上下逆。雲が下から覆い、下から晴れる（雲を抜けて降下する演出）
  };

  // 雲の遷移の影の色と縁の光の色の既定値（rgb、画面で見える値 sRGB）
  // 影は青空を映した青灰色、縁の光は日差しを受けた温かい白
  static inline const Vector4 kDefaultCloudShadowColor = {0.74f, 0.80f, 0.91f, 1.0f};
  static inline const Vector4 kDefaultCloudGlowColor = {1.0f, 0.97f, 0.88f, 1.0f};

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

  /// <summary>
  /// 雲の遷移の影の色と縁の光の色を設定する（雲の本体の色は Start の color）
  /// </summary>
  void SetCloudShade(const Vector4 &shadowColor, const Vector4 &glowColor) {
    cloudShadowColor_ = shadowColor;
    cloudGlowColor_ = glowColor;
  }

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
  bool IsCloudType_() const;
  void ApplyToSprite_();
  static float ApplyEasing_(float t, EasingType easing);
  void ApplyByType_(float eased);

  SpriteRenderer *spriteRenderer_ = nullptr;
  std::unique_ptr<Sprite> sprite_ = nullptr;

  // 雲遷移用の全画面スプライト（UIEffect のシェーダー内で雲を生成して描く）
  std::unique_ptr<Sprite> cloudSprite_ = nullptr;
  // 雲のノイズはシェーダー内で計算するのでテクスチャは使わない。Sprite がテクスチャを
  // 必要とするため、最小の白テクスチャを渡しておく
  static inline const std::string kCloudTexturePath_ = "resources/white1x1.png";
  // UIEffect.PS.hlsl の effectType（2 = 雲ディゾルブ）
  static constexpr int kCloudEffectType_ = 2;
  // 覆いきる直前に縁の光を消していく幅（進行度 1 - この値 から 1 にかけて弱める）
  float cloudEdgeWidth_ = 0.1f;
  // 縁の光の強さ（覆うとき / 晴れるとき）
  float cloudGlowCovering_ = 1.0f;
  float cloudGlowClearing_ = 1.0f;
  // 影の色と縁の光の色
  Vector4 cloudShadowColor_ = kDefaultCloudShadowColor;
  Vector4 cloudGlowColor_ = kDefaultCloudGlowColor;
  // 雲を流すための経過時間（秒）。雲が見えている間だけ進める
  float cloudTime_ = 0.0f;

  std::string texturePath_;

  State state_ = State::Idle;

  Vector4 color_{};

  float alpha_ = 0.0f;
  float speed_ = 1.0f;
  float target_ = 0.0f;
};
