#include "GameHud.h"
#include "Actor/Boss.h"
#include "Actor/Player.h"
#include "Audio/SoundManager.h"
#include "Math/MathUtil.h"
#include "Scene/UiPlate.h"
#include "Score/ScoreKeeper.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>

namespace {
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
const float kFrameSec = 1.0f / 60.0f;
const float kScreenWidth = 1280.0f;
const float kScreenHeight = 720.0f;
const std::string kDir = "resources/UI/Score/";
// 文字の画像の縁の余白（px）。make_intro_text.py の縁のぼかし半径 × 3
const float kTextPad = 12.0f;
const float kMaxLockWordPad = 18.0f;

//---- HP（左下）----
const float kHpLeft = 40.0f;
const float kHpCenterY = 672.0f;
const float kHpWidth = 280.0f;
const float kHpHeight = 12.0f;
// 表示している HP が本当の HP へ近づく速さ（1 秒あたりの割合）
const float kHpFollowRate = 6.0f;

//---- スコア（右下）----
const float kScoreRight = kScreenWidth - 40.0f; // 数字の右端
const float kScoreBottom = kScreenHeight - 30.0f; // 数字の下端
const float kScoreSize = 40.0f;      // 数字の大きさ（px）
const float kScoreLabelGap = 12.0f;  // 「スコア」と数字の間（px）
// 数字の縦の中心は、下端から文字の大きさの半分ほど上
const float kScoreCenterY = kScoreBottom - kScoreSize * 0.5f;
const float kScoreLabelCenterY = kScoreBottom - 18.0f * 0.5f; // 「スコア」18px の縦の中心
// 数え上げ: 1 フレームに残りの差のこの割合だけ近づく（最低 1 点）
const float kScoreCountRate = 0.15f;

//---- ロック数（照準の下）----
const float kLockOffsetY = 46.0f;   // 照準の中心から印の並びの中心まで
const float kLockSpacing = 14.0f;   // 印の間隔（中心どうし、px）
const float kLockFadeSec = 0.12f;   // 現れる・消えるまでの時間（秒）
const float kLockPopSec = 0.18f;    // ロックした印が大きくなってから戻るまで（秒）
const float kLockPopScale = 0.6f;   // ロックした瞬間に大きくなる量（1 に足す倍率）
const float kLockRed[3] = {1.0f, 0.25f, 0.25f};

//---- 得点 ----
const float kPopupBaseSize = 22.0f;  // 1 体の文字の大きさ（px）
const float kPopupSizePerKill = 4.0f; // 1 体増えるごとに大きくする量（px）
const float kPopupOffsetY = -28.0f;   // 倒した敵の位置からずらす量（px、上が負）
const float kPopupRise = 22.0f;       // 消えるまでに上がる量（px）
const float kPopupSec = 0.9f;         // 出ている時間（秒）
const float kPopupFadeInSec = 0.06f;
const float kPopupFadeOutSec = 0.3f;
const float kPopupPopScale = 0.35f;   // 出た瞬間に大きくなる量（1 に足す倍率）
const float kPopupPopSec = 0.15f;
// 最大ロックのカード
const float kCardSec = 1.6f;
const float kCardDigitSize = 54.0f;
const float kCardLineWidth = 180.0f;
const float kCardLineHeight = 1.0f;
const float kCardWordOffsetY = -26.0f;  // 線から言葉の縦の中心まで
const float kCardDigitOffsetY = 32.0f;  // 線から得点の縦の中心まで
const float kCardMargin = 120.0f;       // 画面の端から離す量（px）
// 締めの音（ロック音を 1 オクターブ上のド・ミで重ねる）
const float kCardChordPitches[2] = {2.0f, 2.5198f};

//---- ボスの体力（上の真ん中）----
const float kBossWidth = 400.0f;
const float kBossHeight = 10.0f;
const float kBossTop = 52.0f;
const float kBossLabelCenterY = 34.0f;
const float kBossFadeSec = 0.4f;

//---- バー ----
const float kBarBaseAlpha = 0.72f; // 暗い下地の不透明度
const float kBarFrameAlpha = 0.8f; // 金の枠の不透明度
const float kBarInset = 2.0f;      // 枠から中身までの隙間（px）

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

Vector4 ToColor(const float color[3], float alpha) {
  return {color[0], color[1], color[2], alpha};
}
} // namespace

GameHud::GameHud() = default;

GameHud::~GameHud() = default;

void GameHud::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer](const std::string &path,
                                 const Vector2 &anchor) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint(anchor);
    return sprite;
  };
  const Vector2 kLeftCenter = {0.0f, 0.5f};
  const Vector2 kCenter = {0.5f, 0.5f};

  InitializeBar(hpBar_, spriteRenderer);
  scoreLabel_ = create(kDir + "hud_score.png", kLeftCenter);
  scoreDigits_.Initialize(spriteRenderer, DigitText::Style::Gold, 12);
  for (int i = 0; i < kLockMarkCount; ++i) {
    lockOnMarks_[i] = create(kDir + "lock_on.png", kCenter);
    lockOffMarks_[i] = create(kDir + "lock_off.png", kCenter);
  }
  for (Popup &popup : popups_) {
    popup.digits = std::make_unique<DigitText>();
    popup.digits->Initialize(spriteRenderer, DigitText::Style::Glow, 8);
  }
  maxLockWord_ = create(kDir + "max_lock_word.png", kCenter);
  maxLockLine_ = create("resources/white1x1.png", kCenter);
  maxLockDigits_.Initialize(spriteRenderer, DigitText::Style::Glow, 8);
  InitializeBar(bossBar_, spriteRenderer);
  bossLabel_ = create(kDir + "hud_boss.png", kLeftCenter);
  Reset();
}

void GameHud::Reset() {
  shownHpRatio_ = 1.0f;
  shownScore_ = 0.0f;
  lockPopTimers_.fill(kLockPopSec);
  lockCount_ = 0;
  lockAlpha_ = 0.0f;
  for (Popup &popup : popups_) {
    popup.isActive = false;
  }
  maxLockCard_.isActive = false;
  isBossShown_ = false;
  bossAlpha_ = 0.0f;
  shownBossRatio_ = 1.0f;
}

void GameHud::Update(const Player &player, ScoreKeeper &scoreKeeper,
                     const Boss *boss, const Matrix4x4 &viewProjection,
                     float alpha) {
  alpha_ = alpha;
  UpdateHpBar(player);
  UpdateScore(scoreKeeper.GetScore());
  UpdateLockMarks(player);
  UpdatePopups(scoreKeeper, viewProjection);
  UpdateBossBar(boss);
}

void GameHud::UpdateHpBar(const Player &player) {
  const float ratio =
      std::clamp(static_cast<float>(player.GetHp()) /
                     static_cast<float>(player.GetMaxHp()),
                 0.0f, 1.0f);
  shownHpRatio_ += (ratio - shownHpRatio_) *
                   (std::min)(kHpFollowRate * kFrameSec, 1.0f);
  PlaceBar(hpBar_, kHpLeft, kHpCenterY - kHpHeight * 0.5f, kHpWidth,
           kHpHeight, shownHpRatio_, UiColor::kCyan, alpha_);
}

void GameHud::UpdateScore(int score) {
  // 本当のスコアまで数え上げる
  const float diff = static_cast<float>(score) - shownScore_;
  if (diff > 0.0f) {
    shownScore_ += (std::min)(diff, (std::max)(diff * kScoreCountRate, 1.0f));
  } else {
    shownScore_ = static_cast<float>(score);
  }
  const std::string text =
      DigitText::FormatNumber(static_cast<int>(shownScore_));
  const Vector4 white = {1.0f, 1.0f, 1.0f, alpha_};
  scoreDigits_.Set(text, {kScoreRight, kScoreCenterY}, kScoreSize, 1.0f,
                   white);
  // 「スコア」は数字の左に、下をそろえて置く
  const float labelRight =
      kScoreRight - scoreDigits_.GetWidth(text, kScoreSize) - kScoreLabelGap;
  const float labelWidth = scoreLabel_->GetSize().x - kTextPad * 2.0f;
  scoreLabel_->SetPosition(
      {labelRight - labelWidth - kTextPad, kScoreLabelCenterY});
  scoreLabel_->SetColor(white);
  scoreLabel_->Update(kUvTransform);
}

void GameHud::UpdateLockMarks(const Player &player) {
  const int count = (std::min)(player.GetLockingCount(), kLockMarkCount);
  // 新しくロックした印を大きくする
  for (int i = lockCount_; i < count; ++i) {
    lockPopTimers_[i] = 0.0f;
  }
  lockCount_ = count;
  for (float &timer : lockPopTimers_) {
    timer = (std::min)(timer + kFrameSec, kLockPopSec);
  }

  // ロック中だけ現す（撃つとロックが消えるので一緒に消える）
  const bool isShown = player.IsLockOnMode() || count > 0;
  const float step = kLockFadeSec > 0.0f ? kFrameSec / kLockFadeSec : 1.0f;
  lockAlpha_ = std::clamp(lockAlpha_ + (isShown ? step : -step), 0.0f, 1.0f);

  const Vector2 reticle = player.GetReticlePosition();
  const float y = reticle.y + kLockOffsetY;
  const float left =
      reticle.x - kLockSpacing * static_cast<float>(kLockMarkCount - 1) * 0.5f;
  const float alpha = lockAlpha_ * alpha_;
  for (int i = 0; i < kLockMarkCount; ++i) {
    const Vector2 pos = {left + kLockSpacing * static_cast<float>(i), y};
    const float pop =
        1.0f + kLockPopScale * (1.0f - EaseOutCubic(lockPopTimers_[i] / kLockPopSec));
    lockOnMarks_[i]->SetPosition(pos);
    lockOnMarks_[i]->SetScale({pop, pop});
    lockOnMarks_[i]->SetColor(ToColor(kLockRed, alpha));
    lockOnMarks_[i]->Update(kUvTransform);
    lockOffMarks_[i]->SetPosition(pos);
    lockOffMarks_[i]->SetColor({1.0f, 1.0f, 1.0f, alpha});
    lockOffMarks_[i]->Update(kUvTransform);
  }
}

void GameHud::UpdatePopups(ScoreKeeper &scoreKeeper,
                           const Matrix4x4 &viewProjection) {
  // 点が決まった射撃ごとに、空いている枠へ文字を出す（8 体は最大ロックのカード）
  for (const ScoreKeeper::ScoreEvent &event : scoreKeeper.ConsumeEvents()) {
    const std::string text = DigitText::FormatNumber(event.points);
    if (event.kills >= ScoreKeeper::kMaxLockKills) {
      maxLockCard_ = {true, event.position, text, 0.0f};
      for (float pitch : kCardChordPitches) {
        SoundManager::GetInstance()->PlaySE("lockon", 1.0f, pitch);
      }
      continue;
    }
    Popup *slot = &popups_[0];
    for (Popup &popup : popups_) {
      if (!popup.isActive) {
        slot = &popup;
        break;
      }
      // 空きがなければ、いちばん古い文字を使い回す
      if (popup.timer > slot->timer) {
        slot = &popup;
      }
    }
    slot->isActive = true;
    slot->worldPosition = event.position;
    slot->text = text;
    slot->size = kPopupBaseSize +
                 kPopupSizePerKill * static_cast<float>(event.kills - 1);
    slot->timer = 0.0f;
  }

  for (Popup &popup : popups_) {
    if (!popup.isActive) {
      continue;
    }
    popup.timer += kFrameSec;
    if (popup.timer >= kPopupSec) {
      popup.isActive = false;
      continue;
    }
    const Vector2 screen =
        WorldToScreen(popup.worldPosition, viewProjection, kScreenWidth,
                      kScreenHeight);
    const float t = popup.timer;
    const float alpha =
        Progress(t, 0.0f, kPopupFadeInSec) *
        (1.0f - Progress(t, kPopupSec - kPopupFadeOutSec, kPopupFadeOutSec)) *
        alpha_;
    const float pop =
        1.0f + kPopupPopScale * (1.0f - EaseOutCubic(Progress(t, 0.0f, kPopupPopSec)));
    const float rise = kPopupRise * EaseOutCubic(Progress(t, 0.0f, kPopupSec));
    popup.digits->Set(popup.text,
                      {screen.x, screen.y + kPopupOffsetY - rise},
                      popup.size * pop, 0.5f, {1.0f, 1.0f, 1.0f, alpha});
  }

  if (maxLockCard_.isActive) {
    maxLockCard_.timer += kFrameSec;
    if (maxLockCard_.timer >= kCardSec) {
      maxLockCard_.isActive = false;
    } else {
      Vector2 screen = WorldToScreen(maxLockCard_.worldPosition,
                                     viewProjection, kScreenWidth,
                                     kScreenHeight);
      // カードが画面からはみ出さないよう、端から離す
      screen.x = std::clamp(screen.x, kCardMargin, kScreenWidth - kCardMargin);
      screen.y = std::clamp(screen.y, kCardMargin, kScreenHeight - kCardMargin);
      const float t = maxLockCard_.timer;
      const float alpha =
          Progress(t, 0.0f, kPopupFadeInSec) *
          (1.0f - Progress(t, kCardSec - kPopupFadeOutSec, kPopupFadeOutSec)) *
          alpha_;
      const float pop = 1.0f + kPopupPopScale *
                                   (1.0f - EaseOutCubic(Progress(t, 0.0f, kPopupPopSec)));
      // 線は真ん中から左右へ伸びる
      const float lineWidth =
          kCardLineWidth * EaseOutCubic(Progress(t, 0.0f, kPopupPopSec * 2.0f));
      maxLockLine_->SetPosition(screen);
      maxLockLine_->SetSize({lineWidth, kCardLineHeight});
      maxLockLine_->SetColor(ToColor(UiColor::kGold, alpha));
      maxLockLine_->Update(kUvTransform);
      maxLockWord_->SetPosition({screen.x, screen.y + kCardWordOffsetY});
      maxLockWord_->SetColor({1.0f, 1.0f, 1.0f, alpha});
      maxLockWord_->Update(kUvTransform);
      maxLockDigits_.Set(maxLockCard_.text,
                         {screen.x, screen.y + kCardDigitOffsetY},
                         kCardDigitSize * pop, 0.5f, {1.0f, 1.0f, 1.0f, alpha});
    }
  }
}

void GameHud::UpdateBossBar(const Boss *boss) {
  const bool isAlive = boss && !boss->IsDead();
  if (isAlive && !isBossShown_) {
    isBossShown_ = true;
    shownBossRatio_ = 1.0f;
  }
  const float step = kBossFadeSec > 0.0f ? kFrameSec / kBossFadeSec : 1.0f;
  bossAlpha_ = std::clamp(bossAlpha_ + (isAlive ? step : -step), 0.0f, 1.0f);
  if (isAlive && boss->GetMaxHP() > 0) {
    const float ratio = std::clamp(static_cast<float>(boss->GetHP()) /
                                       static_cast<float>(boss->GetMaxHP()),
                                   0.0f, 1.0f);
    shownBossRatio_ += (ratio - shownBossRatio_) *
                       (std::min)(kHpFollowRate * kFrameSec, 1.0f);
  }
  const float alpha = bossAlpha_ * alpha_;
  const float left = (kScreenWidth - kBossWidth) * 0.5f;
  PlaceBar(bossBar_, left, kBossTop, kBossWidth, kBossHeight, shownBossRatio_,
           UiColor::kBossRed, alpha);
  bossLabel_->SetPosition({left - kTextPad, kBossLabelCenterY});
  bossLabel_->SetColor({1.0f, 1.0f, 1.0f, alpha});
  bossLabel_->Update(kUvTransform);
}

void GameHud::InitializeBar(Bar &bar, SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer]() {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, "resources/white1x1.png");
    sprite->SetAnchorPoint({0.0f, 0.0f});
    return sprite;
  };
  bar.base = create();
  for (auto &edge : bar.frame) {
    edge = create();
  }
  bar.fill = create();
}

void GameHud::PlaceBar(Bar &bar, float left, float top, float width,
                       float height, float ratio, const float fillColor[3],
                       float alpha) {
  auto place = [](Sprite &sprite, const Vector2 &pos, const Vector2 &size,
                  const Vector4 &color) {
    sprite.SetPosition(pos);
    sprite.SetSize(size);
    sprite.SetColor(color);
    sprite.Update(kUvTransform);
  };
  const float kLine = 1.0f;
  place(*bar.base, {left, top}, {width, height},
        ToColor(UiColor::kPlate, kBarBaseAlpha * alpha));
  const Vector4 frameColor = ToColor(UiColor::kGold, kBarFrameAlpha * alpha);
  place(*bar.frame[0], {left, top}, {width, kLine}, frameColor);
  place(*bar.frame[1], {left, top + height - kLine}, {width, kLine},
        frameColor);
  place(*bar.frame[2], {left, top}, {kLine, height}, frameColor);
  place(*bar.frame[3], {left + width - kLine, top}, {kLine, height},
        frameColor);
  const float innerWidth = width - kBarInset * 2.0f;
  place(*bar.fill, {left + kBarInset, top + kBarInset},
        {innerWidth * std::clamp(ratio, 0.0f, 1.0f), height - kBarInset * 2.0f},
        ToColor(fillColor, alpha));
}

void GameHud::DrawBar(Bar &bar) {
  bar.base->Draw();
  bar.fill->Draw();
  for (auto &edge : bar.frame) {
    edge->Draw();
  }
}

void GameHud::Draw() {
  if (alpha_ <= 0.0f) {
    return;
  }
  DrawBar(hpBar_);
  scoreLabel_->Draw();
  scoreDigits_.Draw();
  if (bossAlpha_ > 0.0f) {
    DrawBar(bossBar_);
    bossLabel_->Draw();
  }
  if (lockAlpha_ > 0.0f) {
    for (int i = 0; i < kLockMarkCount; ++i) {
      if (i < lockCount_) {
        lockOnMarks_[i]->Draw();
      } else {
        lockOffMarks_[i]->Draw();
      }
    }
  }
  for (Popup &popup : popups_) {
    if (popup.isActive) {
      popup.digits->Draw();
    }
  }
  if (maxLockCard_.isActive) {
    maxLockLine_->Draw();
    maxLockWord_->Draw();
    maxLockDigits_.Draw();
  }
}
