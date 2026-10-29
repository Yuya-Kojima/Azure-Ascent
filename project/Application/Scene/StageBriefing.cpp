#include "StageBriefing.h"
#include "Framework/MenuInput.h"
#include "Input/Input.h"
#include "Math/MathUtil.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>

namespace {
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
const float kFrameSec = 1.0f / 60.0f;
const std::string kDir = "resources/UI/Score/";
// 文字の画像の縁の余白（px）。make_intro_text.py の縁のぼかし半径 × 3
const float kTextPad = 12.0f;
const float kTitlePad = 15.0f;
const float kChoicePad = 15.0f;
const float kRankPad = 30.0f;
const float kRankImageSize = 120.0f; // ランクの文字の画像に描いた大きさ（px）

//---- 配置（1280x720 の px）----
const float kPlateLeft = 760.0f;
const float kPlateTop = 40.0f;
const float kPlateBottom = 680.0f;
const float kContentLeft = 804.0f;   // 板の左端 + 中の余白 44
const float kContentRight = 1236.0f; // 画面の右端 - 中の余白 44
const float kPlateSlide = 40.0f;     // 板が現れるときに右からずれてくる量
// 1. 空域名
const float kTitleY = 88.0f;
const float kTitleLineY = 116.0f;
const float kLineHeight = 1.0f;
const float kLineAlpha = 0.6f;
// 2. 説明文（17px、行間 1.7）
const float kDescTop = 146.0f;
const float kDescStep = 17.0f * 1.7f;
// 3. ベスト
const float kBestLabelY = 230.0f;
const float kBestRankSize = 64.0f;
const float kBestRankCenterY = 278.0f;
const float kBestValueLeft = 880.0f;  // スコアと撃墜率の左端
const float kBestScoreY = 266.0f;
const float kBestScoreSize = 30.0f;
const float kBestRateY = 300.0f;
const float kBestRateSize = 18.0f;
const float kBestRateGap = 8.0f;      // 「撃墜率」と数の間
// 4. 次のランク
const float kNextLabelY = 350.0f;
const float kNeedTop = 380.0f;
const float kNeedStep = 34.0f;
const float kNeedRankSize = 22.0f;
const float kNeedGap = 8.0f;          // 文字どうしの間
const float kNeedSize = 28.0f;        // あとどれだけかの数の大きさ（px）
// 5. 選択肢
const float kChoiceY = 636.0f;
const float kChoiceGap = 56.0f;
const float kUnderlineOffset = 24.0f;
const float kUnderlineHeight = 2.0f;
const float kUnfocusedAlpha = 0.6f;

//---- 時間（秒）----
const float kPlateSec = 0.3f;       // 板と空域名が現れきるまで
const float kContentStart = 0.15f;  // 中身が現れ始める時刻
const float kContentSec = 0.25f;    // 中身が現れきるまで
const float kChoiceStart = 0.3f;    // 選択肢が現れ始める時刻
const float kChoiceSec = 0.25f;     // 選択肢が現れきるまで（ここから選べる）
const float kLeaveSec = 0.2f;       // 選んだあと消えきるまで
const float kFocusMoveSec = 0.12f;

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

std::string RankTexture(Rank rank) {
  switch (rank) {
  case Rank::S:
    return kDir + "rank_s.png";
  case Rank::A:
    return kDir + "rank_a.png";
  case Rank::B:
    return kDir + "rank_b.png";
  case Rank::C:
  default:
    return kDir + "rank_c.png";
  }
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

float InkWidth(const Sprite &sprite, float pad) {
  return sprite.GetSize().x - pad * 2.0f;
}

/// <summary>
/// ランクの文字を、文字の左端 inkLeft・縦の中心 centerY に、大きさ size（px）で置く
/// </summary>
void PlaceRank(Sprite &sprite, float inkLeft, float centerY, float size,
               float alpha) {
  const float scale = size / kRankImageSize;
  const float inkWidth = InkWidth(sprite, kRankPad) * scale;
  sprite.SetPosition({inkLeft + inkWidth * 0.5f, centerY});
  sprite.SetScale({scale, scale});
  sprite.SetColor({1.0f, 1.0f, 1.0f, alpha});
  sprite.Update(kUvTransform);
}
} // namespace

StageBriefing::StageBriefing() = default;

StageBriefing::~StageBriefing() = default;

void StageBriefing::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer](const std::string &path,
                                 const Vector2 &anchor) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint(anchor);
    return sprite;
  };
  const Vector2 kLeftCenter = {0.0f, 0.5f};
  const Vector2 kCenter = {0.5f, 0.5f};

  plate_.Initialize(spriteRenderer);
  title_ = create("resources/UI/GameOver/route_title.png", kLeftCenter);
  titleLine_ = create("resources/white1x1.png", kLeftCenter);
  for (size_t i = 0; i < descLines_.size(); ++i) {
    descLines_[i] =
        create(kDir + "brief_desc" + std::to_string(i) + ".png", kLeftCenter);
  }
  bestLabel_ = create(kDir + "label_best.png", kLeftCenter);
  noRecord_ = create(kDir + "brief_no_record.png", kLeftCenter);
  bestRank_ = create(kDir + "rank_c.png", kCenter);
  bestScore_.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
  bestKillRateLabel_ = create(kDir + "brief_kill_rate.png", kLeftCenter);
  bestKillRate_.Initialize(spriteRenderer, DigitText::Style::Pale, 6);
  nextLabel_ = create(kDir + "brief_next.png", kLeftCenter);
  needScoreLabel_ = create(kDir + "brief_need_score.png", kLeftCenter);
  needRateLabel_ = create(kDir + "brief_need_rate.png", kLeftCenter);
  for (NeedRow &row : needRows_) {
    row.digits.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
    row.rank = create(kDir + "rank_c.png", kCenter);
    row.until = create(kDir + "brief_until.png", kLeftCenter);
  }
  sortie_ = create(kDir + "brief_sortie.png", kLeftCenter);
  back_ = create(kDir + "brief_back.png", kLeftCenter);
  underline_ = create("resources/white1x1.png", kLeftCenter);
}

void StageBriefing::Start(const std::string &stageName,
                          const ScoreConfig &config) {
  isActive_ = true;
  isLeaving_ = false;
  timer_ = 0.0f;
  leaveTimer_ = 0.0f;
  pendingChoice_ = Choice::None;
  focused_ = Choice::Sortie;
  focusPos_ = 0.0f;

  record_ = StageRecord::Load(stageName);
  bestRank_->ChangeTexture(RankTexture(record_.bestRank));

  // 次のランク（ベストの総合ランクの 1 つ上）まで、スコアと撃墜率があとどれだけ要るか
  for (NeedRow &row : needRows_) {
    row.isShown = false;
  }
  if (record_.hasRecord && record_.bestRank != Rank::S) {
    const Rank next = static_cast<Rank>(static_cast<int>(record_.bestRank) + 1);
    const int scoreNeed = config.GetScoreBorder(next) - record_.bestScore;
    // ％は切り上げ（あと 4.2% なら 5%）
    const float kPercentEpsilon = 0.0001f;
    const int rateNeed = static_cast<int>(std::ceil(
        (config.GetKillRateBorder(next) - record_.bestKillRate) * 100.0f -
        kPercentEpsilon));
    int rowIndex = 0;
    if (scoreNeed > 0) {
      NeedRow &row = needRows_[rowIndex++];
      row.isShown = true;
      row.isScore = true;
      row.text = DigitText::FormatNumber(scoreNeed);
    }
    if (rateNeed > 0) {
      NeedRow &row = needRows_[rowIndex++];
      row.isShown = true;
      row.isScore = false;
      row.text = std::to_string(rateNeed) + "%";
    }
    for (NeedRow &row : needRows_) {
      row.rank->ChangeTexture(RankTexture(next));
    }
  }
  UpdateSprites();
}

void StageBriefing::Update(Input *input) {
  if (!isActive_) {
    return;
  }
  timer_ += kFrameSec;
  if (isLeaving_) {
    leaveTimer_ += kFrameSec;
    if (leaveTimer_ >= kLeaveSec) {
      isActive_ = false;
    }
    UpdateSprites();
    return;
  }

  // 下線を、選んでいる方へ動かす
  const float focusTarget = (focused_ == Choice::Sortie) ? 0.0f : 1.0f;
  const float focusStep = kFrameSec / kFocusMoveSec;
  focusPos_ += std::clamp(focusTarget - focusPos_, -focusStep, focusStep);

  if (input) {
    const float readySec = kChoiceStart + kChoiceSec;
    if (timer_ < readySec) {
      // 現れきる前に決定を押すと、現れきった所まで飛ばす（その押下では選ばない）
      if (MenuInput::IsDecideTriggered(input)) {
        timer_ = readySec;
      }
    } else {
      if (MenuInput::IsLeftTriggered(input) ||
          MenuInput::IsRightTriggered(input) ||
          MenuInput::IsUpTriggered(input) || MenuInput::IsDownTriggered(input)) {
        focused_ = (focused_ == Choice::Sortie) ? Choice::Back : Choice::Sortie;
      }
      if (MenuInput::IsDecideTriggered(input)) {
        pendingChoice_ = focused_;
        isLeaving_ = true;
        leaveTimer_ = 0.0f;
      }
    }
  }
  UpdateSprites();
}

StageBriefing::Choice StageBriefing::ConsumeChoice() {
  Choice choice = pendingChoice_;
  pendingChoice_ = Choice::None;
  return choice;
}

void StageBriefing::UpdateSprites() {
  const float t = timer_;
  const float leave = 1.0f - Progress(leaveTimer_, 0.0f, kLeaveSec);
  const float plateIn = EaseOutCubic(Progress(t, 0.0f, kPlateSec));
  const float slide = kPlateSlide * (1.0f - plateIn);
  const float plateAlpha = plateIn * leave;
  const float contentAlpha =
      EaseOutCubic(Progress(t, kContentStart, kContentSec)) * leave;
  const float choiceAlpha =
      EaseOutCubic(Progress(t, kChoiceStart, kChoiceSec)) * leave;
  const float left = kContentLeft + slide;

  //---- 1. 空域名 ----
  plate_.Set(kPlateLeft + slide, kPlateTop, kPlateBottom, plateAlpha);
  PlaceText(*title_, left, kTitleY, kTitlePad, plateAlpha);
  titleLine_->SetPosition({left, kTitleLineY});
  titleLine_->SetSize({kContentRight - kContentLeft, kLineHeight});
  titleLine_->SetColor({UiColor::kGold[0], UiColor::kGold[1],
                        UiColor::kGold[2], kLineAlpha * plateAlpha});
  titleLine_->Update(kUvTransform);

  //---- 2. 説明文 ----
  for (size_t i = 0; i < descLines_.size(); ++i) {
    PlaceText(*descLines_[i], left,
              kDescTop + kDescStep * static_cast<float>(i), kTextPad,
              contentAlpha);
  }

  //---- 3. ベスト ----
  PlaceText(*bestLabel_, left, kBestLabelY, kTextPad, contentAlpha);
  PlaceText(*noRecord_, left, kBestScoreY, kTextPad, contentAlpha);
  PlaceRank(*bestRank_, left, kBestRankCenterY, kBestRankSize, contentAlpha);
  const float valueLeft = kBestValueLeft + slide;
  const Vector4 contentColor = {1.0f, 1.0f, 1.0f, contentAlpha};
  bestScore_.Set(DigitText::FormatNumber(record_.bestScore),
                 {valueLeft, kBestScoreY}, kBestScoreSize, 0.0f, contentColor);
  PlaceText(*bestKillRateLabel_, valueLeft, kBestRateY, kTextPad,
            contentAlpha);
  const int bestPercent =
      static_cast<int>(std::floor(record_.bestKillRate * 100.0f));
  bestKillRate_.Set(
      std::to_string(bestPercent) + "%",
      {valueLeft + InkWidth(*bestKillRateLabel_, kTextPad) + kBestRateGap,
       kBestRateY},
      kBestRateSize, 0.0f, contentColor);

  //---- 4. 次のランク ----
  PlaceText(*nextLabel_, left, kNextLabelY, kTextPad, contentAlpha);
  for (size_t i = 0; i < needRows_.size(); ++i) {
    NeedRow &row = needRows_[i];
    if (!row.isShown) {
      continue;
    }
    const float y = kNeedTop + kNeedStep * static_cast<float>(i);
    PlaceRank(*row.rank, left, y, kNeedRankSize, contentAlpha);
    float x = left + InkWidth(*row.rank, kRankPad) * kNeedRankSize /
                         kRankImageSize +
              kNeedGap;
    PlaceText(*row.until, x, y, kTextPad, contentAlpha);
    x += InkWidth(*row.until, kTextPad) + kNeedGap * 2.0f;
    Sprite &label = row.isScore ? *needScoreLabel_ : *needRateLabel_;
    PlaceText(label, x, y, kTextPad, contentAlpha);
    x += InkWidth(label, kTextPad) + kNeedGap;
    row.digits.Set(row.text, {x, y}, kNeedSize, 0.0f, contentColor);
  }

  //---- 5. 選択肢 ----
  const float sortieWidth = InkWidth(*sortie_, kChoicePad);
  const float backWidth = InkWidth(*back_, kChoicePad);
  const float backLeft = left + sortieWidth + kChoiceGap;
  PlaceText(*sortie_, left, kChoiceY, kChoicePad,
            Lerp(kUnfocusedAlpha, 1.0f, 1.0f - focusPos_) * choiceAlpha);
  PlaceText(*back_, backLeft, kChoiceY, kChoicePad,
            Lerp(kUnfocusedAlpha, 1.0f, focusPos_) * choiceAlpha);
  const float eased = EaseInOutCubic(focusPos_);
  underline_->SetPosition(
      {Lerp(left, backLeft, eased), kChoiceY + kUnderlineOffset});
  underline_->SetSize({Lerp(sortieWidth, backWidth, eased), kUnderlineHeight});
  underline_->SetColor({UiColor::kGold[0], UiColor::kGold[1],
                        UiColor::kGold[2], choiceAlpha});
  underline_->Update(kUvTransform);
}

void StageBriefing::Draw() {
  if (!isActive_) {
    return;
  }
  plate_.Draw();
  title_->Draw();
  titleLine_->Draw();
  for (auto &line : descLines_) {
    line->Draw();
  }
  bestLabel_->Draw();
  if (!record_.hasRecord) {
    noRecord_->Draw();
  } else {
    // 記録があるときだけ、ベストと次のランクを出す
    bestRank_->Draw();
    bestScore_.Draw();
    bestKillRateLabel_->Draw();
    bestKillRate_.Draw();
    if (needRows_[0].isShown) {
      nextLabel_->Draw();
      for (NeedRow &row : needRows_) {
        if (!row.isShown) {
          continue;
        }
        row.rank->Draw();
        row.until->Draw();
        (row.isScore ? needScoreLabel_ : needRateLabel_)->Draw();
        row.digits.Draw();
      }
    }
  }
  underline_->Draw();
  sortie_->Draw();
  back_->Draw();
}
