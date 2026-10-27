#include "SkipHold.h"
#include "Core/WindowSystem.h"
#include "Framework/MenuInput.h"
#include "Sprite/Sprite.h"
#include <algorithm>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 文字の画像の端から文字までの余白（px）。make_intro_text.py のぼかし半径 4 の 3 倍
const float kTextPadPx = 12.0f;
// UV は画像全体をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
} // namespace

SkipHold::SkipHold() = default;

SkipHold::~SkipHold() = default;

void SkipHold::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer](const char *path, const Vector2 &anchor) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint(anchor);
    return sprite;
  };
  text_ = create("resources/UI/Common/skip_hold.png", {1.0f, 1.0f});
  track_ = create("resources/white1x1.png", {0.0f, 0.5f});
  fill_ = create("resources/white1x1.png", {0.0f, 0.5f});
  Start();
}

void SkipHold::Start() {
  holdTimer_ = 0.0f;
  lingerTimer_ = 0.0f;
  alpha_ = 0.0f;
  isArmed_ = false;
}

bool SkipHold::Update(Input *input, bool canSkip) {
  const bool isHeld = input && MenuInput::IsSkipHeld(input);
  if (!isArmed_ && !isHeld) {
    isArmed_ = true;
  }

  //===========================
  // 押している時間：押している間は伸び、伸びきったら飛ばす。離すと縮む
  //===========================
  bool isSkipped = false;
  if (isArmed_ && isHeld && canSkip) {
    holdTimer_ += kFrameSec;
    lingerTimer_ = lingerSec_;
    if (holdTimer_ >= holdSec_) {
      holdTimer_ = holdSec_;
      isSkipped = true;
      // 飛ばしたら表示を消していき、押しっぱなしで次を数えないようにする
      isArmed_ = false;
      lingerTimer_ = 0.0f;
    }
  } else {
    holdTimer_ = (std::max)(0.0f, holdTimer_ - kFrameSec * holdSec_ / drainSec_);
    lingerTimer_ = canSkip ? (std::max)(0.0f, lingerTimer_ - kFrameSec) : 0.0f;
  }

  //===========================
  // 不透明度：残す時間の間は現れ、過ぎたら消える
  //===========================
  if (lingerTimer_ > 0.0f) {
    alpha_ = (std::min)(1.0f, alpha_ + kFrameSec / fadeInSec_);
  } else {
    alpha_ = (std::max)(0.0f, alpha_ - kFrameSec / fadeOutSec_);
  }

  UpdateSprites();
  return isSkipped;
}

void SkipHold::UpdateSprites() {
  // 線の右端を画面の右下に置き、長さは文字の幅にそろえる
  const float textWidth = text_->GetSize().x - kTextPadPx * 2.0f;
  const float lineRight = WindowSystem::kClientWidth - marginRight_;
  const float lineLeft = lineRight - textWidth;
  const float lineY = WindowSystem::kClientHeight - marginBottom_;

  // 文字：画像の右下（余白を除いた文字の右下）が、線の右端の lineGap_ 上に来るように置く
  text_->SetPosition({lineRight + kTextPadPx, lineY - lineGap_ + kTextPadPx});
  text_->SetColor({1.0f, 1.0f, 1.0f, alpha_});
  text_->Update(kUvTransform);

  // 下地：伸びきったときの長さで薄く出す
  track_->SetPosition({lineLeft, lineY});
  track_->SetSize({textWidth, lineThickness_});
  track_->SetColor({lineColor_.x, lineColor_.y, lineColor_.z,
                    lineColor_.w * trackAlpha_ * alpha_});
  track_->Update(kUvTransform);

  // 伸びる線：押している時間の割合で左から伸びる
  const float ratio = std::clamp(holdTimer_ / holdSec_, 0.0f, 1.0f);
  fill_->SetPosition({lineLeft, lineY});
  fill_->SetSize({textWidth * ratio, lineThickness_});
  fill_->SetColor({lineColor_.x, lineColor_.y, lineColor_.z,
                   lineColor_.w * alpha_});
  fill_->Update(kUvTransform);
}

void SkipHold::Draw() {
  if (alpha_ <= 0.0f) {
    return;
  }
  track_->Draw();
  fill_->Draw();
  text_->Draw();
}

void SkipHold::DrawImGui() {
#ifdef USE_IMGUI
  ImGui::DragFloat("Hold Sec", &holdSec_, 0.01f, 0.1f, 3.0f);
  ImGui::DragFloat("Drain Sec", &drainSec_, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("Fade In Sec", &fadeInSec_, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("Fade Out Sec", &fadeOutSec_, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("Linger Sec", &lingerSec_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Margin Right", &marginRight_, 1.0f, 0.0f, 640.0f);
  ImGui::DragFloat("Margin Bottom", &marginBottom_, 1.0f, 0.0f, 360.0f);
  ImGui::DragFloat("Line Gap", &lineGap_, 0.5f, 0.0f, 40.0f);
  ImGui::DragFloat("Line Thickness", &lineThickness_, 0.1f, 0.5f, 10.0f);
  ImGui::DragFloat("Track Alpha", &trackAlpha_, 0.01f, 0.0f, 1.0f);
  ImGui::ColorEdit4("Line Color", &lineColor_.x);
#endif
}
