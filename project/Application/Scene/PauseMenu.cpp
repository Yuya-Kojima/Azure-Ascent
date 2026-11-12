#include "PauseMenu.h"
#include "Audio/SoundManager.h"
#include "Framework/MenuInput.h"
#include "Input/Input.h"
#include "Math/MathUtil.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace {
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
const float kFrameSec = 1.0f / 60.0f;
const std::string kDir = "resources/UI/Pause/";
// 文字の画像の縁の余白（px）。make_intro_text.py の縁のぼかし半径 × 3
const float kTextPad = 12.0f;
const float kChoicePad = 15.0f;

//---- 配置（1280x720 の px）----
const float kScreenWidth = 1280.0f;
const float kScreenHeight = 720.0f;
const float kDimAlpha = 0.35f; // 画面を暗くする黒の不透明度
const float kPlateLeft = 900.0f;
const float kPlateTop = 200.0f;
const float kPlateBottom = 500.0f;
const float kContentLeft = 944.0f;   // 板の左端 + 中の余白 44
const float kContentRight = 1236.0f; // 画面の右端 - 中の余白 44
const float kPlateSlide = 40.0f;     // 板が現れるときに右からずれてくる量
// 1. 「一時停止」と線
const float kTitleY = 248.0f;
const float kTitleLineY = 276.0f;
const float kLineHeight = 1.0f;
const float kLineAlpha = 0.6f;
// 2. 3 つの選択肢（縦）
const float kItemTop = 330.0f;
const float kItemStep = 56.0f;
// 3. 確認
const float kWarnY = 322.0f;
const float kAskY = 362.0f;
const float kConfirmChoiceY = 442.0f;
const float kConfirmChoiceGap = 56.0f; // 「はい」と「いいえ」の間
// 選択肢の共通
const float kUnderlineOffset = 22.0f; // 選択肢の縦の中心から下線まで
const float kUnderlineHeight = 2.0f;
const float kUnfocusedAlpha = 0.6f;

//---- 時間（秒）----
const float kOpenSec = 0.2f;       // 板が現れきるまで
const float kPageFadeSec = 0.12f;  // 板の中身が変わってから現れきるまで
const float kFocusMoveSec = 0.12f; // 下線が隣の選択肢へ移りきるまで

/// <summary>
/// 時刻 time が 0 から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float sec) {
  if (sec <= 0.0f) {
    return 1.0f;
  }
  return std::clamp(time / sec, 0.0f, 1.0f);
}

/// <summary>
/// 文字の画像を、縁の余白を除いた文字の左端 inkLeft・縦の中心 centerY に置く
/// </summary>
void PlaceText(Sprite &sprite, float inkLeft, float centerY, float pad,
               float alpha) {
  sprite.SetPosition({inkLeft - pad, centerY});
  sprite.SetColor({1.0f, 1.0f, 1.0f, alpha});
  sprite.Update(kUvTransform);
}

/// <summary>
/// 縁の余白を除いた文字の幅
/// </summary>
float InkWidth(const Sprite &sprite, float pad) {
  return sprite.GetSize().x - pad * 2.0f;
}

/// <summary>
/// 下線の位置 pos を target へ、1 フレーム分だけ近づける（1 つ隣まで kFocusMoveSec）
/// </summary>
float MoveFocus(float pos, float target) {
  const float step = kFrameSec / kFocusMoveSec;
  return pos + std::clamp(target - pos, -step, step);
}

/// <summary>
/// 下線が index の選択肢にどれだけ乗っているか（0〜1）
/// </summary>
float FocusWeight(float focusPos, int index) {
  return std::clamp(1.0f - std::abs(focusPos - static_cast<float>(index)), 0.0f, 1.0f);
}
} // namespace

PauseMenu::PauseMenu() = default;

PauseMenu::~PauseMenu() = default;

void PauseMenu::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer](const std::string &path,
                                 const Vector2 &anchor) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint(anchor);
    return sprite;
  };
  const Vector2 kLeftTop = {0.0f, 0.0f};
  const Vector2 kLeftCenter = {0.0f, 0.5f};

  dim_ = create("resources/white1x1.png", kLeftTop);
  plate_.Initialize(spriteRenderer);
  title_ = create(kDir + "pause_title.png", kLeftCenter);
  titleLine_ = create("resources/white1x1.png", kLeftCenter);
  items_[kResumeIndex] = create(kDir + "pause_resume.png", kLeftCenter);
  items_[kAgainIndex] = create("resources/UI/Score/choice_again.png", kLeftCenter);
  items_[kBaseIndex] = create("resources/UI/Score/choice_base.png", kLeftCenter);
  warn_ = create(kDir + "pause_warn.png", kLeftCenter);
  askAgain_ = create(kDir + "pause_ask_again.png", kLeftCenter);
  askBase_ = create(kDir + "pause_ask_base.png", kLeftCenter);
  yes_ = create(kDir + "pause_yes.png", kLeftCenter);
  no_ = create(kDir + "pause_no.png", kLeftCenter);
  underline_ = create("resources/white1x1.png", kLeftCenter);
}

void PauseMenu::Open() {
  isOpen_ = true;
  timer_ = 0.0f;
  pendingChoice_ = Choice::None;
  hasChosen_ = false;
  focused_ = kResumeIndex;
  focusPos_ = static_cast<float>(kResumeIndex);
  ShowPage(Page::Menu);
  UpdateSprites();
}

void PauseMenu::Close() {
  isOpen_ = false;
  pendingChoice_ = Choice::None;
  hasChosen_ = false;
}

void PauseMenu::Update(Input *input) {
  if (!isOpen_) {
    return;
  }
  timer_ += kFrameSec;
  pageTimer_ += kFrameSec;
  focusPos_ = MoveFocus(focusPos_, static_cast<float>(focused_));
  confirmPos_ = MoveFocus(confirmPos_, isConfirmYes_ ? 0.0f : 1.0f);

  if (input && !hasChosen_) {
    if (page_ == Page::Menu) {
      UpdateMenuPage(input);
    } else {
      UpdateConfirmPage(input);
    }
  }
  if (isOpen_) {
    UpdateSprites();
  }
}

PauseMenu::Choice PauseMenu::ConsumeChoice() {
  Choice choice = pendingChoice_;
  pendingChoice_ = Choice::None;
  return choice;
}

void PauseMenu::UpdateMenuPage(Input *input) {
  if (MenuInput::IsBackTriggered(input)) {
    pendingChoice_ = Choice::Resume;
    isOpen_ = false;
    return;
  }
  if (MenuInput::IsUpTriggered(input)) {
    focused_ = (focused_ + kItemCount - 1) % kItemCount;
  }
  if (MenuInput::IsDownTriggered(input)) {
    focused_ = (focused_ + 1) % kItemCount;
  }
  if (!MenuInput::IsDecideTriggered(input)) {
    return;
  }
  SoundManager::GetInstance()->PlaySE("ui_decide");
  if (focused_ == kResumeIndex) {
    pendingChoice_ = Choice::Resume;
    isOpen_ = false;
    return;
  }
  // リトライ・拠点へ戻るは、確認を挟む（最初のカーソルは「いいえ」）
  ShowPage(Page::Confirm);
}

void PauseMenu::UpdateConfirmPage(Input *input) {
  if (MenuInput::IsBackTriggered(input)) {
    ShowPage(Page::Menu);
    return;
  }
  if (MenuInput::IsLeftTriggered(input) || MenuInput::IsRightTriggered(input) ||
      MenuInput::IsUpTriggered(input) || MenuInput::IsDownTriggered(input)) {
    isConfirmYes_ = !isConfirmYes_;
  }
  if (!MenuInput::IsDecideTriggered(input)) {
    return;
  }
  SoundManager::GetInstance()->PlaySE("ui_decide");
  if (!isConfirmYes_) {
    ShowPage(Page::Menu);
    return;
  }
  pendingChoice_ = (focused_ == kAgainIndex) ? Choice::Again : Choice::Base;
  hasChosen_ = true;
}

void PauseMenu::ShowPage(Page page) {
  page_ = page;
  pageTimer_ = 0.0f;
  isConfirmYes_ = false;
  confirmPos_ = 1.0f;
}

void PauseMenu::UpdateSprites() {
  const float openIn = EaseOutCubic(Progress(timer_, kOpenSec));
  const float pageIn = EaseOutCubic(Progress(pageTimer_, kPageFadeSec));
  const float slide = kPlateSlide * (1.0f - openIn);
  const float left = kContentLeft + slide;
  const float contentAlpha = openIn * pageIn;

  dim_->SetPosition({0.0f, 0.0f});
  dim_->SetSize({kScreenWidth, kScreenHeight});
  dim_->SetColor({0.0f, 0.0f, 0.0f, kDimAlpha * openIn});
  dim_->Update(kUvTransform);

  //---- 1. 板と「一時停止」----
  plate_.Set(kPlateLeft + slide, kPlateTop, kPlateBottom, openIn);
  PlaceText(*title_, left, kTitleY, kChoicePad, openIn);
  titleLine_->SetPosition({left, kTitleLineY});
  titleLine_->SetSize({kContentRight - kContentLeft, kLineHeight});
  titleLine_->SetColor({UiColor::kGold[0], UiColor::kGold[1], UiColor::kGold[2],
                        kLineAlpha * openIn});
  titleLine_->Update(kUvTransform);

  const Vector4 lineColor = {UiColor::kGold[0], UiColor::kGold[1],
                             UiColor::kGold[2], contentAlpha};
  if (page_ == Page::Menu) {
    //---- 2. 3 つの選択肢（下線は選んでいる物の下へ動く）----
    for (int i = 0; i < kItemCount; ++i) {
      const float y = kItemTop + kItemStep * static_cast<float>(i);
      const float focus = FocusWeight(focusPos_, i);
      PlaceText(*items_[i], left, y, kChoicePad,
                Lerp(kUnfocusedAlpha, 1.0f, focus) * contentAlpha);
    }
    const int lower = std::clamp(static_cast<int>(std::floor(focusPos_)), 0, kItemCount - 1);
    const int upper = (std::min)(lower + 1, kItemCount - 1);
    const float blend = EaseInOutCubic(focusPos_ - static_cast<float>(lower));
    const float width = Lerp(InkWidth(*items_[lower], kChoicePad),
                             InkWidth(*items_[upper], kChoicePad), blend);
    underline_->SetPosition(
        {left, kItemTop + kItemStep * (static_cast<float>(lower) + blend) + kUnderlineOffset});
    underline_->SetSize({width, kUnderlineHeight});
  } else {
    //---- 3. 確認（「はい」「いいえ」は横に並べる）----
    PlaceText(*warn_, left, kWarnY, kTextPad, contentAlpha);
    PlaceText(*askAgain_, left, kAskY, kTextPad, contentAlpha);
    PlaceText(*askBase_, left, kAskY, kTextPad, contentAlpha);
    const float yesWidth = InkWidth(*yes_, kChoicePad);
    const float noWidth = InkWidth(*no_, kChoicePad);
    const float noLeft = left + yesWidth + kConfirmChoiceGap;
    PlaceText(*yes_, left, kConfirmChoiceY, kChoicePad,
              Lerp(kUnfocusedAlpha, 1.0f, 1.0f - confirmPos_) * contentAlpha);
    PlaceText(*no_, noLeft, kConfirmChoiceY, kChoicePad,
              Lerp(kUnfocusedAlpha, 1.0f, confirmPos_) * contentAlpha);
    const float eased = EaseInOutCubic(confirmPos_);
    underline_->SetPosition(
        {Lerp(left, noLeft, eased), kConfirmChoiceY + kUnderlineOffset});
    underline_->SetSize({Lerp(yesWidth, noWidth, eased), kUnderlineHeight});
  }
  underline_->SetColor(lineColor);
  underline_->Update(kUvTransform);
}

void PauseMenu::Draw() {
  if (!isOpen_) {
    return;
  }
  dim_->Draw();
  plate_.Draw();
  title_->Draw();
  titleLine_->Draw();
  if (page_ == Page::Menu) {
    for (auto &item : items_) {
      item->Draw();
    }
  } else {
    warn_->Draw();
    if (focused_ == kAgainIndex) {
      askAgain_->Draw();
    } else {
      askBase_->Draw();
    }
    yes_->Draw();
    no_->Draw();
  }
  underline_->Draw();
}
