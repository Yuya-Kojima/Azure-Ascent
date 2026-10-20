#include "StageIntroText.h"
#include "Core/WindowSystem.h"
#include "Sprite/Sprite.h"
#include <algorithm>

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
} // namespace

StageIntroText::StageIntroText() = default;

StageIntroText::~StageIntroText() = default;

void StageIntroText::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer](const std::string &path) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint({0.5f, 0.5f});
    return sprite;
  };
  line_ = create("resources/white1x1.png");
  stageName_ = create("resources/UI/StageIntro/stage_name.png");
  objective_ = create("resources/UI/StageIntro/objective.png");
}

void StageIntroText::Start() {
  timer_ = 0.0f;
  isActive_ = true;
}

void StageIntroText::Update() {
  if (!isActive_) {
    return;
  }

  timer_ += kFrameSec;
  if (timer_ >= cardOutTime_ + cardOutSec_) {
    isActive_ = false;
    return;
  }

  //===========================
  // 横線：中央から左右へ伸び、消えるときは中央へ縮む
  //===========================
  float grow = EaseOutCubic(Progress(timer_, cardInTime_, lineGrowSec_));
  float out = EaseInOutCubic(Progress(timer_, cardOutTime_, cardOutSec_));
  float lineLength = lineWidth_ * grow * (1.0f - out);
  line_->SetPosition({kScreenCenterX, lineY_});
  line_->SetSize({lineLength, lineThickness_});
  line_->SetColor(lineColor_);
  line_->Update(kUvTransform);

  //===========================
  // 文字：線の上下から少し線へ寄りながら現れ、線と一緒に消える
  // ステージ名は上から、目標は下から寄る
  //===========================
  float textIn =
      EaseOutCubic(Progress(timer_, cardInTime_ + textInDelay_, textInSec_));
  float alpha = textIn * (1.0f - out);
  float offset = textRiseY_ * (1.0f - textIn);
  ApplySprite(*stageName_, {kScreenCenterX, stageNameY_ - offset}, alpha);
  ApplySprite(*objective_, {kScreenCenterX, objectiveY_ + offset}, alpha);
}

void StageIntroText::Draw() {
  if (!isActive_ || timer_ < cardInTime_) {
    return;
  }
  line_->Draw();
  stageName_->Draw();
  objective_->Draw();
}

void StageIntroText::DrawImGui() {
#ifdef USE_IMGUI
  ImGui::SeparatorText("Objective Card");
  ImGui::DragFloat("Card In Time", &cardInTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Line Grow Sec", &lineGrowSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Text In Delay", &textInDelay_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Text In Sec", &textInSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Text Rise Y", &textRiseY_, 0.5f, 0.0f, 100.0f);
  ImGui::DragFloat("Card Out Time", &cardOutTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Card Out Sec", &cardOutSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Line Y", &lineY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Line Width", &lineWidth_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Line Thickness", &lineThickness_, 0.1f, 0.5f, 10.0f);
  ImGui::DragFloat("Stage Name Y", &stageNameY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Objective Y", &objectiveY_, 1.0f, 0.0f, 720.0f);
  ImGui::ColorEdit4("Line Color", &lineColor_.x);
#endif
}
