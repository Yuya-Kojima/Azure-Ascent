#include "CaptionCard.h"
#include "Core/WindowSystem.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 画面の横の中央（px）
const float kScreenCenterX = WindowSystem::kClientWidth * 0.5f;
// UV は画像全体をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

/// <summary>
/// スプライトの位置と不透明度を決めて更新する（色は白に不透明度を掛ける）
/// </summary>
void ApplySprite(Sprite &sprite, const Vector2 &position, float alpha) {
  sprite.SetPosition(position);
  sprite.SetColor({1.0f, 1.0f, 1.0f, alpha});
  sprite.Update(kUvTransform);
}

/// <summary>
/// 文字の画像の、横の中央から割合 reveal の幅だけを切り出して出す（中央を基準に置いたまま）
/// 画素がぼけないよう、切り出す幅と画面の上の左上の角を整数の px にそろえる
/// </summary>
void ApplyReveal(Sprite &sprite, const Vector2 &fullSize, float reveal,
                 const Vector2 &position, float alpha) {
  const float width = std::floor(fullSize.x * reveal);
  sprite.SetTextureLeftTop({std::floor((fullSize.x - width) * 0.5f), 0.0f});
  sprite.SetTextureSize({width, fullSize.y});
  sprite.SetSize({width, fullSize.y});
  const Vector2 leftTop = {std::floor(position.x - width * 0.5f),
                           std::floor(position.y - fullSize.y * 0.5f)};
  ApplySprite(sprite,
              {leftTop.x + width * 0.5f, leftTop.y + fullSize.y * 0.5f},
              width > 0.0f ? alpha : 0.0f);
}
} // namespace

CaptionCard::CaptionCard() = default;

CaptionCard::~CaptionCard() = default;

void CaptionCard::Initialize(SpriteRenderer *spriteRenderer,
                             const Settings &settings) {
  settings_ = settings;
  auto create = [spriteRenderer](const std::string &path) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint({0.5f, 0.5f});
    return sprite;
  };
  line_ = create("resources/white1x1.png");
  upper_ = create(settings_.upperPath);
  lower_ = create(settings_.lowerPath);
  upperSize_ = upper_->GetSize();
  lowerSize_ = lower_->GetSize();
  if (settings_.revealByLight) {
    sparkLeft_ = create(settings_.sparkPath);
    sparkRight_ = create(settings_.sparkPath);
  }
}

void CaptionCard::Start() {
  timer_ = 0.0f;
  isActive_ = true;
}

void CaptionCard::Update() {
  if (!isActive_) {
    return;
  }

  timer_ += kFrameSec;
  if (timer_ >= GetEndTime()) {
    isActive_ = false;
    return;
  }
  Apply();
}

void CaptionCard::SetTime(float time) {
  timer_ = time;
  isActive_ = timer_ < GetEndTime();
  if (isActive_) {
    Apply();
  }
}

void CaptionCard::SetTiming(const Timing &timing) {
  settings_.cardInTime = timing.cardInTime;
  settings_.lineGrowSec = timing.lineGrowSec;
  settings_.textInDelay = timing.textInDelay;
  settings_.cardOutTime = timing.cardOutTime;
  settings_.cardOutSec = timing.cardOutSec;
}

void CaptionCard::SetLayout(const Layout &layout) {
  settings_.lineY = layout.lineY;
  settings_.lineWidth = layout.lineWidth;
  settings_.upperY = layout.upperY;
  settings_.lowerY = layout.lowerY;
}

void CaptionCard::Apply() {
  const Settings &s = settings_;

  //===========================
  // 横線：中央から左右へ伸び、消えるときは中央へ縮む
  //===========================
  float grow = EaseOutCubic(Progress(timer_, s.cardInTime, s.lineGrowSec));
  float out = EaseInOutCubic(Progress(timer_, s.cardOutTime, s.cardOutSec));
  float lineLength = s.lineWidth * grow * (1.0f - out);
  line_->SetPosition({kScreenCenterX, s.lineY});
  line_->SetSize({lineLength, s.lineThickness});
  line_->SetColor(s.lineColor);
  line_->Update(kUvTransform);

  if (s.revealByLight) {
    //===========================
    // 光の粒：伸びている横線の両端に乗り、伸びきってから消える
    //===========================
    const float halfLength = lineLength * 0.5f;
    sparkAlpha_ = (grow > 0.0f ? 1.0f : 0.0f) *
                  (1.0f - Progress(timer_, s.cardInTime + s.lineGrowSec,
                                   s.sparkFadeSec));
    const Vector4 sparkColor = {s.sparkColor.x, s.sparkColor.y, s.sparkColor.z,
                                s.sparkColor.w * sparkAlpha_};
    for (Sprite *spark : {sparkLeft_.get(), sparkRight_.get()}) {
      const float side = spark == sparkLeft_.get() ? -1.0f : 1.0f;
      spark->SetPosition({kScreenCenterX + side * halfLength, s.lineY});
      spark->SetSize({s.sparkSize, s.sparkSize});
      spark->SetColor(sparkColor);
      spark->Update(kUvTransform);
    }

    //===========================
    // 文字：上の文字は横線と同じ割合で、下の文字は textInDelay だけ遅れて、中央から左右へ現れる
    //===========================
    const float lowerGrow = EaseOutCubic(
        Progress(timer_, s.cardInTime + s.textInDelay, s.lineGrowSec));
    const float alpha = 1.0f - out;
    ApplyReveal(*upper_, upperSize_, grow, {kScreenCenterX, s.upperY}, alpha);
    ApplyReveal(*lower_, lowerSize_, lowerGrow, {kScreenCenterX, s.lowerY},
                alpha);
    return;
  }

  //===========================
  // 文字：線の上下から少し線へ寄りながら現れ、線と一緒に消える
  // 上の文字は上から、下の文字は下から寄る
  //===========================
  float textIn = EaseOutCubic(
      Progress(timer_, s.cardInTime + s.textInDelay, s.textInSec));
  float alpha = textIn * (1.0f - out);
  float offset = s.textRiseY * (1.0f - textIn);
  ApplySprite(*upper_, {kScreenCenterX, s.upperY - offset}, alpha);
  ApplySprite(*lower_, {kScreenCenterX, s.lowerY + offset}, alpha);
}

void CaptionCard::Draw() {
  if (!isActive_ || timer_ < settings_.cardInTime) {
    return;
  }
  line_->Draw();
  upper_->Draw();
  lower_->Draw();
  if (settings_.revealByLight && sparkAlpha_ > 0.0f) {
    sparkLeft_->Draw();
    sparkRight_->Draw();
  }
}

void CaptionCard::DrawImGui() {
#ifdef USE_IMGUI
  Settings &s = settings_;
  ImGui::DragFloat("Card In Time", &s.cardInTime, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Line Grow Sec", &s.lineGrowSec, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Text In Delay", &s.textInDelay, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Text In Sec", &s.textInSec, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Text Rise Y", &s.textRiseY, 0.5f, 0.0f, 100.0f);
  ImGui::DragFloat("Card Out Time", &s.cardOutTime, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Card Out Sec", &s.cardOutSec, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Line Y", &s.lineY, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Line Width", &s.lineWidth, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Line Thickness", &s.lineThickness, 0.1f, 0.5f, 10.0f);
  ImGui::DragFloat("Upper Y", &s.upperY, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Lower Y", &s.lowerY, 1.0f, 0.0f, 720.0f);
  ImGui::ColorEdit4("Line Color", &s.lineColor.x);
  if (s.revealByLight) {
    ImGui::DragFloat("Spark Size", &s.sparkSize, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Spark Fade Sec", &s.sparkFadeSec, 0.01f, 0.01f, 2.0f);
    ImGui::ColorEdit4("Spark Color", &s.sparkColor.x);
  }
#endif
}
