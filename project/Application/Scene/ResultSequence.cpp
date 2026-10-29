#include "ResultSequence.h"
#include "Framework/MenuInput.h"
#include "Input/Input.h"
#include "Math/MathUtil.h"
#include "Sprite/Sprite.h"
#include <algorithm>

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
const float kPlateLeft = 700.0f;
const float kPlateTop = 40.0f;
const float kPlateBottom = 680.0f;
const float kContentLeft = 744.0f;  // 板の左端 + 中の余白 44
const float kContentRight = 1236.0f; // 画面の右端 - 中の余白 44
const float kPlateSlide = 40.0f;     // 板が現れるときに右からずれてくる量
// 1. 空域名
const float kTitleY = 88.0f;
const float kReclaimGap = 12.0f; // 空域名と「奪還」の間
const float kTitleLineY = 116.0f;
const float kLineHeight = 1.0f;
const float kLineAlpha = 0.6f;
// 2. 内訳
const float kRowTop = 150.0f;
const float kRowStep = 40.0f;
const float kRowCountRight = 960.0f; // 数の補足（「40」）の右端
const float kRowUnitGap = 6.0f;      // 数の補足と「体」「回」の間
const float kRowCountSize = 20.0f;
const float kRowPointsSize = 26.0f;
// 3. 細い線
const float kDividerY = 306.0f;
// 4. スコアと撃墜率
const float kScoreY = 340.0f;
const float kKillRateY = 384.0f;
const float kTotalNumberSize = 32.0f;
const float kSmallRankSize = 30.0f;
const float kSmallRankCenterX = kContentRight - 12.0f; // 文字の中心（右端にそろえる）
const float kTotalNumberRight = kContentRight - 40.0f;
const float kKillCountRight = 1060.0f; // 「36 / 40」の右端
// 5. 総合ランク
const float kTotalLabelY = 430.0f;
const float kTotalRankCenterY = 505.0f;
const float kBestLeft = 880.0f;
const float kBestUpdateY = 482.0f;
const float kBestRowY = 518.0f;
const float kBestDigitGap = 10.0f;
const float kBestDigitSize = 20.0f;
// 6. ヒント
const float kHintLabelY = 568.0f;
const float kHintY = 594.0f;
// 7. 選択肢
const float kChoiceY = 646.0f;
const float kChoiceGap = 56.0f;      // 2 つの選択肢の間
const float kUnderlineOffset = 22.0f; // 選択肢の縦の中心から下線まで
const float kUnderlineHeight = 2.0f;
const float kUnfocusedAlpha = 0.6f;

//---- 時間（秒）----
const float kPlateSec = 0.35f;     // 板と空域名が現れきるまで
const float kRowStart = 0.5f;      // 内訳の 1 行目が出る時刻
const float kRowInterval = 0.35f;  // 内訳の行の間隔
const float kFadeSec = 0.15f;      // 1 つの物が現れきるまで
const float kRowCountSec = 0.3f;   // 内訳の点を数え上げる時間
const float kScoreCountSec = 0.5f; // スコアを数え上げる時間
const float kKillCountSec = 0.4f;  // 撃墜率を数え上げる時間
const float kStampSec = 0.2f;      // ランクの文字が大きく出てから落ち着くまで
const float kStampScale = 0.6f;    // ランクの文字が出た瞬間に大きくなる量（1 に足す倍率）
const float kTotalStampSec = 0.3f;
const float kTotalStampScale = 0.8f;
const float kFocusMoveSec = 0.12f; // 下線がもう一方へ移りきるまで
// 出る時刻
const float kRowsEnd = kRowStart + kRowInterval * 4.0f;
const float kScoreTime = kRowsEnd + 0.1f;
const float kKillRateTime = kScoreTime + kScoreCountSec + 0.3f;
const float kTotalTime = kKillRateTime + kKillCountSec + 0.4f;
const float kBestTime = kTotalTime + 0.5f;
const float kHintTime = kBestTime + 0.35f;
const float kChoiceTime = kHintTime + 0.35f;
const float kChoiceFadeSec = 0.3f;

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

/// <summary>
/// ランクの文字の画像
/// </summary>
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
               float alpha, float scale = 1.0f) {
  sprite.SetPosition({inkLeft - pad * scale, centerY});
  sprite.SetScale({scale, scale});
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
/// 撃墜率の％（切り捨て。全機撃墜でなければ 100 にならない）
/// </summary>
int KillPercent(int killed, int spawned) {
  return spawned > 0 ? killed * 100 / spawned : 100;
}
} // namespace

ResultSequence::ResultSequence() = default;

ResultSequence::~ResultSequence() = default;

void ResultSequence::Initialize(SpriteRenderer *spriteRenderer) {
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
  reclaim_ = create(kDir + "result_reclaim.png", kLeftCenter);
  titleLine_ = create("resources/white1x1.png", kLeftCenter);

  const char *labels[kRowCount] = {"label_kills", "label_volley",
                                   "label_max_lock", "label_boss"};
  const char *units[kRowCount] = {"unit_kills", nullptr, "unit_times",
                                  nullptr};
  for (int i = 0; i < kRowCount; ++i) {
    rows_[i].label = create(kDir + labels[i] + ".png", kLeftCenter);
    if (units[i]) {
      rows_[i].unit = create(kDir + units[i] + ".png", kLeftCenter);
    }
    rows_[i].count.Initialize(spriteRenderer, DigitText::Style::Pale, 6);
    rows_[i].points.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
  }
  divider_ = create("resources/white1x1.png", kLeftCenter);
  scoreLabel_ = create(kDir + "label_score.png", kLeftCenter);
  scoreDigits_.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
  scoreRank_ = create(kDir + "rank_c.png", kCenter);
  killRateLabel_ = create(kDir + "label_kill_rate.png", kLeftCenter);
  killCountDigits_.Initialize(spriteRenderer, DigitText::Style::Pale, 12);
  killRateDigits_.Initialize(spriteRenderer, DigitText::Style::Gold, 6);
  killRateRank_ = create(kDir + "rank_c.png", kCenter);
  totalLabel_ = create(kDir + "label_total.png", kLeftCenter);
  totalRank_ = create(kDir + "rank_c.png", kCenter);
  bestUpdate_ = create(kDir + "best_update.png", kLeftCenter);
  bestLabel_ = create(kDir + "label_best.png", kLeftCenter);
  prevLabel_ = create(kDir + "label_prev.png", kLeftCenter);
  bestDigits_.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
  hintLabel_ = create(kDir + "label_hint.png", kLeftCenter);
  for (size_t i = 0; i < hintSections_.size(); ++i) {
    hintSections_[i] = create(
        kDir + "hint_section" + std::to_string(i) + ".png", kLeftCenter);
  }
  hintPerfect_ = create(kDir + "hint_perfect.png", kLeftCenter);
  again_ = create(kDir + "choice_again.png", kLeftCenter);
  base_ = create(kDir + "choice_base.png", kLeftCenter);
  underline_ = create("resources/white1x1.png", kLeftCenter);
}

void ResultSequence::Start(const Result &result) {
  isActive_ = true;
  timer_ = 0.0f;
  result_ = result;
  pendingChoice_ = Choice::None;
  hasChosen_ = false;
  focused_ = Choice::Again;
  focusPos_ = 0.0f;

  const ScoreKeeper::Breakdown &b = result_.breakdown;
  rows_[0].countValue = b.kills;
  rows_[0].pointsValue = b.killPoints;
  rows_[1].countValue = -1;
  rows_[1].pointsValue = b.volleyPoints;
  rows_[2].countValue = b.maxLockCount;
  rows_[2].pointsValue = b.maxLockPoints;
  rows_[3].countValue = -1;
  rows_[3].pointsValue = b.bossPoints;

  scoreRank_->ChangeTexture(RankTexture(result_.scoreRank));
  killRateRank_->ChangeTexture(RankTexture(result_.killRateRank));
  totalRank_->ChangeTexture(RankTexture(result_.overallRank));
  UpdateSprites();
}

void ResultSequence::Update(Input *input) {
  if (!isActive_) {
    return;
  }
  timer_ += kFrameSec;
  UpdateMenu(input);
  UpdateSprites();
}

ResultSequence::Choice ResultSequence::ConsumeChoice() {
  Choice choice = pendingChoice_;
  pendingChoice_ = Choice::None;
  return choice;
}

float ResultSequence::GetReadySec() const {
  return kChoiceTime + kChoiceFadeSec;
}

void ResultSequence::UpdateMenu(Input *input) {
  // 下線を、選んでいる方へ動かす
  const float focusTarget = (focused_ == Choice::Again) ? 0.0f : 1.0f;
  const float focusStep = kFrameSec / kFocusMoveSec;
  focusPos_ += std::clamp(focusTarget - focusPos_, -focusStep, focusStep);

  if (!input || hasChosen_) {
    return;
  }
  // 選択肢が現れきる前に決定を押すと、現れきった所まで飛ばす（その押下では選ばない）
  if (timer_ < GetReadySec()) {
    if (MenuInput::IsDecideTriggered(input)) {
      timer_ = GetReadySec();
    }
    return;
  }
  if (MenuInput::IsLeftTriggered(input) || MenuInput::IsRightTriggered(input) ||
      MenuInput::IsUpTriggered(input) || MenuInput::IsDownTriggered(input)) {
    focused_ = (focused_ == Choice::Again) ? Choice::Base : Choice::Again;
  }
  if (MenuInput::IsDecideTriggered(input)) {
    pendingChoice_ = focused_;
    hasChosen_ = true;
  }
}

void ResultSequence::UpdateSprites() {
  const float t = timer_;

  //---- 1. 板と空域名 ----
  const float plateIn = EaseOutCubic(Progress(t, 0.0f, kPlateSec));
  const float slide = kPlateSlide * (1.0f - plateIn);
  plate_.Set(kPlateLeft + slide, kPlateTop, kPlateBottom, plateIn);
  PlaceText(*title_, kContentLeft + slide, kTitleY, kTitlePad, plateIn);
  PlaceText(*reclaim_,
            kContentLeft + slide + InkWidth(*title_, kTitlePad) + kReclaimGap,
            kTitleY, kTextPad, plateIn);
  titleLine_->SetPosition({kContentLeft + slide, kTitleLineY});
  titleLine_->SetSize({kContentRight - kContentLeft, kLineHeight});
  titleLine_->SetColor({UiColor::kGold[0], UiColor::kGold[1],
                        UiColor::kGold[2], kLineAlpha * plateIn});
  titleLine_->Update(kUvTransform);

  //---- 2. 内訳を上から 1 行ずつ数え上げる ----
  for (int i = 0; i < kRowCount; ++i) {
    Row &row = rows_[i];
    const float start = kRowStart + kRowInterval * static_cast<float>(i);
    const float alpha = EaseOutCubic(Progress(t, start, kFadeSec));
    const float count = EaseOutCubic(Progress(t, start, kRowCountSec));
    const float y = kRowTop + kRowStep * static_cast<float>(i);
    PlaceText(*row.label, kContentLeft, y, kTextPad, alpha);
    if (row.countValue >= 0) {
      const std::string countText = std::to_string(row.countValue);
      row.count.Set(countText, {kRowCountRight, y}, kRowCountSize, 1.0f,
                    {1.0f, 1.0f, 1.0f, alpha});
      if (row.unit) {
        PlaceText(*row.unit, kRowCountRight + kRowUnitGap, y, kTextPad, alpha);
      }
    }
    const int points =
        static_cast<int>(static_cast<float>(row.pointsValue) * count);
    row.points.Set(DigitText::FormatNumber(points), {kContentRight, y},
                   kRowPointsSize, 1.0f, {1.0f, 1.0f, 1.0f, alpha});
  }

  //---- 3. 細い線 ----
  const float dividerAlpha = Progress(t, kRowsEnd, kFadeSec);
  divider_->SetPosition({kContentLeft, kDividerY});
  divider_->SetSize({kContentRight - kContentLeft, kLineHeight});
  divider_->SetColor({UiColor::kGold[0], UiColor::kGold[1], UiColor::kGold[2],
                      kLineAlpha * dividerAlpha});
  divider_->Update(kUvTransform);

  // ランクの文字: 数え上げが終わった瞬間に大きく出て、落ち着く
  auto placeRank = [](Sprite &sprite, const Vector2 &center, float size,
                      float time, float stampSec, float stampScale) {
    const float appear = Progress(time, 0.0f, kFadeSec);
    const float stamp =
        1.0f + stampScale * (1.0f - EaseOutCubic(Progress(time, 0.0f, stampSec)));
    const float scale = size / kRankImageSize * stamp;
    sprite.SetPosition(center);
    sprite.SetScale({scale, scale});
    sprite.SetColor({1.0f, 1.0f, 1.0f, time >= 0.0f ? appear : 0.0f});
    sprite.Update(kUvTransform);
  };

  //---- 4. スコアと撃墜率 ----
  const int total = result_.breakdown.GetTotal();
  const float scoreAlpha = Progress(t, kScoreTime, kFadeSec);
  const float scoreCount = EaseOutCubic(Progress(t, kScoreTime, kScoreCountSec));
  PlaceText(*scoreLabel_, kContentLeft, kScoreY, kTextPad, scoreAlpha);
  scoreDigits_.Set(
      DigitText::FormatNumber(static_cast<int>(static_cast<float>(total) * scoreCount)),
      {kTotalNumberRight, kScoreY}, kTotalNumberSize, 1.0f,
      {1.0f, 1.0f, 1.0f, scoreAlpha});
  placeRank(*scoreRank_, {kSmallRankCenterX, kScoreY}, kSmallRankSize,
            t - (kScoreTime + kScoreCountSec), kStampSec, kStampScale);

  const int killed = result_.breakdown.kills;
  const int spawned = result_.spawned;
  const float killAlpha = Progress(t, kKillRateTime, kFadeSec);
  const float killCount = EaseOutCubic(Progress(t, kKillRateTime, kKillCountSec));
  PlaceText(*killRateLabel_, kContentLeft, kKillRateY, kTextPad, killAlpha);
  killCountDigits_.Set(std::to_string(killed) + " / " + std::to_string(spawned),
                       {kKillCountRight, kKillRateY}, kRowCountSize, 1.0f,
                       {1.0f, 1.0f, 1.0f, killAlpha});
  const int percent = static_cast<int>(
      static_cast<float>(KillPercent(killed, spawned)) * killCount);
  killRateDigits_.Set(std::to_string(percent) + "%",
                      {kTotalNumberRight, kKillRateY}, kTotalNumberSize, 1.0f,
                      {1.0f, 1.0f, 1.0f, killAlpha});
  placeRank(*killRateRank_, {kSmallRankCenterX, kKillRateY}, kSmallRankSize,
            t - (kKillRateTime + kKillCountSec), kStampSec, kStampScale);

  //---- 5. 総合ランクとベスト ----
  const float totalAlpha = Progress(t, kTotalTime, kFadeSec);
  PlaceText(*totalLabel_, kContentLeft, kTotalLabelY, kTextPad, totalAlpha);
  const float totalInk = InkWidth(*totalRank_, kRankPad);
  placeRank(*totalRank_, {kContentLeft + totalInk * 0.5f, kTotalRankCenterY},
            kRankImageSize, t - kTotalTime, kTotalStampSec, kTotalStampScale);

  const float bestAlpha = Progress(t, kBestTime, kFadeSec);
  const Vector4 bestColor = {1.0f, 1.0f, 1.0f, bestAlpha};
  // 更新したら「ベスト更新」と前のベスト、しなければ今のベスト
  Sprite &rowLabel = result_.isNewBest ? *prevLabel_ : *bestLabel_;
  const bool isRowShown = !result_.isNewBest || result_.hasPreviousBest;
  const int rowValue =
      result_.isNewBest ? result_.previousBestScore : result_.bestScore;
  PlaceText(*bestUpdate_, kBestLeft, kBestUpdateY, kTextPad, bestAlpha);
  PlaceText(rowLabel, kBestLeft, kBestRowY, kTextPad,
            isRowShown ? bestAlpha : 0.0f);
  bestDigits_.Set(DigitText::FormatNumber(rowValue),
                  {kBestLeft + InkWidth(rowLabel, kTextPad) + kBestDigitGap,
                   kBestRowY},
                  kBestDigitSize, 0.0f,
                  isRowShown ? bestColor : Vector4{0.0f, 0.0f, 0.0f, 0.0f});

  //---- 6. ヒント ----
  const float hintAlpha = Progress(t, kHintTime, kFadeSec);
  PlaceText(*hintLabel_, kContentLeft, kHintLabelY, kTextPad, hintAlpha);
  for (auto &section : hintSections_) {
    PlaceText(*section, kContentLeft, kHintY, kTextPad, hintAlpha);
  }
  PlaceText(*hintPerfect_, kContentLeft, kHintY, kTextPad, hintAlpha);

  //---- 7. 選択肢 ----
  const float choiceAlpha = EaseOutCubic(Progress(t, kChoiceTime, kChoiceFadeSec));
  const float againFocus = 1.0f - focusPos_;
  const float baseFocus = focusPos_;
  const float againWidth = InkWidth(*again_, kChoicePad);
  const float baseWidth = InkWidth(*base_, kChoicePad);
  const float baseLeft = kContentLeft + againWidth + kChoiceGap;
  PlaceText(*again_, kContentLeft, kChoiceY, kChoicePad,
            Lerp(kUnfocusedAlpha, 1.0f, againFocus) * choiceAlpha);
  PlaceText(*base_, baseLeft, kChoiceY, kChoicePad,
            Lerp(kUnfocusedAlpha, 1.0f, baseFocus) * choiceAlpha);
  const float eased = EaseInOutCubic(focusPos_);
  underline_->SetPosition(
      {Lerp(kContentLeft, baseLeft, eased), kChoiceY + kUnderlineOffset});
  underline_->SetSize({Lerp(againWidth, baseWidth, eased), kUnderlineHeight});
  underline_->SetColor({UiColor::kGold[0], UiColor::kGold[1],
                        UiColor::kGold[2], choiceAlpha});
  underline_->Update(kUvTransform);
}

void ResultSequence::Draw() {
  if (!isActive_) {
    return;
  }
  const float t = timer_;
  plate_.Draw();
  title_->Draw();
  reclaim_->Draw();
  titleLine_->Draw();
  for (int i = 0; i < kRowCount; ++i) {
    if (t < kRowStart + kRowInterval * static_cast<float>(i)) {
      continue;
    }
    Row &row = rows_[i];
    row.label->Draw();
    if (row.countValue >= 0) {
      row.count.Draw();
      if (row.unit) {
        row.unit->Draw();
      }
    }
    row.points.Draw();
  }
  if (t >= kRowsEnd) {
    divider_->Draw();
  }
  if (t >= kScoreTime) {
    scoreLabel_->Draw();
    scoreDigits_.Draw();
  }
  if (t >= kScoreTime + kScoreCountSec) {
    scoreRank_->Draw();
  }
  if (t >= kKillRateTime) {
    killRateLabel_->Draw();
    killCountDigits_.Draw();
    killRateDigits_.Draw();
  }
  if (t >= kKillRateTime + kKillCountSec) {
    killRateRank_->Draw();
  }
  if (t >= kTotalTime) {
    totalLabel_->Draw();
    totalRank_->Draw();
  }
  if (t >= kBestTime) {
    if (result_.isNewBest) {
      bestUpdate_->Draw();
      if (result_.hasPreviousBest) {
        prevLabel_->Draw();
        bestDigits_.Draw();
      }
    } else {
      bestLabel_->Draw();
      bestDigits_.Draw();
    }
  }
  if (t >= kHintTime) {
    hintLabel_->Draw();
    if (result_.hintSection < 0) {
      hintPerfect_->Draw();
    } else {
      const int index = (std::min)(result_.hintSection,
                                   static_cast<int>(hintSections_.size()) - 1);
      hintSections_[index]->Draw();
    }
  }
  if (t >= kChoiceTime) {
    underline_->Draw();
    again_->Draw();
    base_->Draw();
  }
}
