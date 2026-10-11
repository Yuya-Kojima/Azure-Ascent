#include "Sprite/Fade.h"
#include "Debug/Logger.h"
#include "Renderer/SpriteRenderer.h"
#include "Sprite/Sprite.h"

#include <algorithm>
#include <cassert>

void Fade::Initialize(SpriteRenderer *spriteRenderer,
                      const std::string &texturePath) {

  spriteRenderer_ = spriteRenderer;
  texturePath_ = texturePath;

  assert(spriteRenderer_ && "Fade::Initialize: spriteRenderer is null");

  sprite_ = std::make_unique<Sprite>();

  sprite_->Initialize(spriteRenderer_, texturePath_);

  sprite_->SetAnchorPoint({0.0f, 0.0f});
  sprite_->SetPosition({0.0f, 0.0f});
  sprite_->SetSize(
      {float(WindowSystem::kClientWidth), float(WindowSystem::kClientHeight)});

  // 雲遷移用の全画面スプライト。ノイズの読み方（拡大・流れ）はシェーダー側で決める
  cloudSprite_ = std::make_unique<Sprite>();
  cloudSprite_->Initialize(spriteRenderer_, kCloudTexturePath_);
  cloudSprite_->SetAnchorPoint({0.0f, 0.0f});
  cloudSprite_->SetPosition({0.0f, 0.0f});
  cloudSprite_->SetSize(
      {float(WindowSystem::kClientWidth), float(WindowSystem::kClientHeight)});
  cloudSprite_->SetUIEffectParams(0.0f, kCloudEffectType_);

  state_ = State::Idle;
  alpha_ = 1.0f;
  target_ = 1.0f;
  speed_ = 0.0f;

  // 黒で初期化
  color_ = Vector4{0.0f, 0.0f, 0.0f, 1.0f};

  ApplyToSprite_();
}

void Fade::Finalize() {
  cloudSprite_.reset();
  sprite_.reset();
  spriteRenderer_ = nullptr;
  texturePath_.clear();
  state_ = State::Idle;
  alpha_ = 0.0f;
  target_ = 0.0f;
  speed_ = 1.0f;
  color_ = Vector4{0.0f, 0.0f, 0.0f, 1.0f};
}

void Fade::StartFadeOut(float durationSec) {
  StartFadeOut(durationSec, type_, Vector4{0, 0, 0, 1},
               EasingType::EaseOutCubic);
}

void Fade::StartFadeIn(float durationSec) {
  StartFadeIn(durationSec, type_, Vector4{0, 0, 0, 1},
              EasingType::EaseOutCubic);
}

void Fade::StartFadeOut(float durationSec, FadeType type, const Vector4 &color,
                        EasingType easing) {
  if (!sprite_)
    return;

  type_ = type;
  color_ = color;
  easing_ = easing;

  durationSec = (std::max)(durationSec, 0.0001f);
  state_ = State::FadingOut;
  target_ = 1.0f;
  speed_ = 1.0f / durationSec;

  ApplyToSprite_();
}

void Fade::StartFadeIn(float durationSec, FadeType type, const Vector4 &color,
                       EasingType easing) {
  if (!sprite_)
    return;

  type_ = type;
  color_ = color;
  easing_ = easing;

  durationSec = (std::max)(durationSec, 0.0001f);
  state_ = State::FadingIn;
  target_ = 0.0f;
  speed_ = 1.0f / durationSec;

  if (alpha_ <= 0.0f) {
    alpha_ = 1.0f;
  }

  ApplyToSprite_();
}

void Fade::Update(float deltaTime) {
  if (!sprite_) {
    return;
  }

  Transform uv{};
  uv.scale = {1.0f, 1.0f, 1.0f};
  uv.rotate = {0.0f, 0.0f, 0.0f};
  uv.translate = {0.0f, 0.0f, 0.0f};
  sprite_->Update(uv);
  cloudSprite_->Update(uv);

  // 雲が見えている間は時間を進めて流し続ける（覆いきって待機している間も含む）
  if (type_ == FadeType::Cloud && alpha_ > 0.0f) {
    cloudTime_ += deltaTime;
    cloudSprite_->SetUIEffectParams(cloudTime_, kCloudEffectType_);
  }

  if (state_ == State::Idle) {
    return;
  }

  // 方向に応じて alpha を動かす
  if (state_ == State::FadingOut) {
    alpha_ += speed_ * deltaTime;
    if (alpha_ >= target_) {
      alpha_ = target_;
      state_ = State::Idle;
    }
  } else if (state_ == State::FadingIn) {
    alpha_ -= speed_ * deltaTime;
    if (alpha_ <= target_) {
      alpha_ = target_;
      state_ = State::Idle;
    }
  }

  alpha_ = std::clamp(alpha_, 0.0f, 1.0f);
  ApplyToSprite_();
}

void Fade::Draw() {
  if (!sprite_) {
    return;
  }

  if (alpha_ <= 0.0f) {
    return;
  }

  if (type_ == FadeType::Cloud) {
    // 雲は UIEffect のパイプラインで描き、後続の 2D 描画のために通常設定へ戻す
    spriteRenderer_->BeginUIEffect();
    cloudSprite_->DrawUIEffect();
    spriteRenderer_->Begin();
    return;
  }

  sprite_->Draw();
}

Fade::Fade() = default;

Fade::~Fade() = default;

void Fade::ApplySolid_(float eased) {
  sprite_->SetAnchorPoint({0.0f, 0.0f});

  Vector4 c = color_;
  c.w = eased;
  sprite_->SetColor(c);

  // フルスクリーン
  sprite_->SetPosition({0.0f, 0.0f});
  sprite_->SetSize(
      {float(WindowSystem::kClientWidth), float(WindowSystem::kClientHeight)});
}

void Fade::ApplyWipeLeft_(float eased) {
  sprite_->SetAnchorPoint({0.0f, 0.0f});

  Vector4 c = color_;
  c.w = 1.0f;
  sprite_->SetColor(c);

  float width = WindowSystem::kClientWidth * eased;

  // 左上から右に伸びる
  sprite_->SetPosition({0.0f, 0.0f});
  sprite_->SetSize({width, float(WindowSystem::kClientHeight)});
}

void Fade::ApplyWipeRight_(float eased) {
  Vector4 c = color_;
  c.w = 1.0f;
  sprite_->SetColor(c);

  float w = float(WindowSystem::kClientWidth) * eased;
  float h = float(WindowSystem::kClientHeight);

  // 右上から左に伸びる（右端を固定）
  sprite_->SetAnchorPoint({1.0f, 0.0f});
  sprite_->SetPosition({float(WindowSystem::kClientWidth), 0.0f});
  sprite_->SetSize({w, h});
}

void Fade::ApplyWipeUp_(float eased) {
  Vector4 c = color_;
  c.w = 1.0f;
  sprite_->SetColor(c);

  float w = float(WindowSystem::kClientWidth);
  float h = float(WindowSystem::kClientHeight) * eased;

  // 下から上に伸びる（下端を固定）
  sprite_->SetAnchorPoint({0.0f, 1.0f});
  sprite_->SetPosition({0.0f, float(WindowSystem::kClientHeight)});
  sprite_->SetSize({w, h});
}

void Fade::ApplyWipeDown_(float eased) {
  Vector4 c = color_;
  c.w = 1.0f;
  sprite_->SetColor(c);

  float w = float(WindowSystem::kClientWidth);
  float h = float(WindowSystem::kClientHeight) * eased;

  // 上から下に伸びる
  sprite_->SetAnchorPoint({0.0f, 0.0f});
  sprite_->SetPosition({0.0f, 0.0f});
  sprite_->SetSize({w, h});
}

void Fade::ApplyCloud_(float eased) {
  // 雲の色（color_ の rgb）。透明度はシェーダー側でしきい値から決める
  Vector4 c = color_;
  c.w = 1.0f;
  cloudSprite_->SetColor(c);

  // 晴れるときは縦の向きを反転して、覆うときと同じく上から晴らす
  const bool isClearing = (state_ == State::FadingIn);
  const float glowStrength =
      isClearing ? cloudGlowClearing_ : cloudGlowCovering_;

  // 進行度をそのまましきい値として渡す
  cloudSprite_->SetDissolveParams(eased, cloudEdgeWidth_, isClearing,
                                  glowStrength);
}

void Fade::ApplyToSprite_() {
  if (!sprite_) {
    return;
  }

  float t = std::clamp(alpha_, 0.0f, 1.0f);
  float eased = ApplyEasing_(t, easing_);

  ApplyByType_(eased);
}

float Fade::ApplyEasing_(float t, EasingType easing) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (easing) {
  case EasingType::Linear:
    return t;
  case EasingType::EaseOutCubic:
    return EaseOutCubic(t);
  case EasingType::EaseInOutCubic:
    return EaseInOutCubic(t);
  case EasingType::EaseOutQuint:
    return EaseOutQuint(t);
  }
  return t;
}

void Fade::ApplyByType_(float eased) {
  switch (type_) {
  case FadeType::Solid:
    ApplySolid_(eased);
    break;

  case FadeType::WipeLeft:
    ApplyWipeLeft_(eased);
    break;

  case FadeType::WipeRight:
    ApplyWipeRight_(eased);
    break;

  case FadeType::WipeUp:
    ApplyWipeUp_(eased);
    break;

  case FadeType::WipeDown:
    ApplyWipeDown_(eased);
    break;

  case FadeType::Cloud:
    ApplyCloud_(eased);
    break;
  }
}
