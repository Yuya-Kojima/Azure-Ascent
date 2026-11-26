#include "BehaviorFighter.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace {

// 1 フレームの秒数
constexpr float kFrameSec = 1.0f / 60.0f;

/// <summary>
/// 敵の基準の位置と向き（レールカメラに沿って進む基準）
/// </summary>
struct BaseFrame {
  Vector3 position;
  Vector3 right;
  Vector3 up;
  Vector3 forward;

  // 基準の右・上・前の向きで offset だけ動かした位置
  Vector3 ToWorld(const Vector3 &offset) const {
    return position + right * offset.x + up * offset.y + forward * offset.z;
  }
};

// 機首を自機へ向ける
void FacePlayer(Enemy &enemy) {
  if (auto player = enemy.GetPlayer()) {
    const Vector3 toPlayer = player->GetTransform().translate - enemy.GetTransform().translate;
    enemy.GetTransform().rotate.y = std::atan2(toPlayer.x, toPlayer.z);
  }
}

} // namespace

/// <summary>
/// 状態の基底クラス。1 フレーム進め、次の状態へ移るならその状態を返す
/// </summary>
class BehaviorFighter::State {
public:
  virtual ~State() = default;

  /// <summary>
  /// 1 フレーム進める。次の状態へ移るなら、その状態を返す（このままなら nullptr）
  /// </summary>
  std::unique_ptr<State> Update(Enemy &enemy, const Config &config, const BaseFrame &base) {
    timer_ += kFrameSec;
    return OnUpdate(enemy, config, base);
  }

protected:
  /// <summary>
  /// この状態の 1 フレームの動き
  /// </summary>
  virtual std::unique_ptr<State> OnUpdate(Enemy &enemy, const Config &config,
                                          const BaseFrame &base) = 0;

  // この状態に入ってからの秒数
  float GetTimer() const { return timer_; }

private:
  float timer_ = 0.0f;
};

namespace {

/// <summary>
/// 離脱：画面奥へ飛び去り、画面の外に出たら消える
/// </summary>
class RetreatState : public BehaviorFighter::State {
protected:
  std::unique_ptr<State> OnUpdate(Enemy &enemy, const BehaviorFighter::Config &,
                                  const BaseFrame &base) override {
    const float kRetreatSpeed = 2.5f; // 前へ飛び去る速さ（m/フレーム）
    const float kRiseSpeed = 0.3f;    // 上へ昇る速さ（m/フレーム）
    enemy.GetTransform().translate += base.forward * kRetreatSpeed + Vector3{0.0f, kRiseSpeed, 0.0f};

    // 少し飛んでから画面の外に出たら消滅（出られなくても時間で消す）
    const float kMinRetreatSec = 0.5f;
    const float kMaxRetreatSec = 5.0f;
    if ((GetTimer() > kMinRetreatSec && enemy.HasLeftScreen()) || GetTimer() > kMaxRetreatSec) {
      enemy.Destroy();
    }
    return nullptr;
  }
};

/// <summary>
/// 回避：決めた方向へロールしながら急加速し、時間が来たら離脱へ
/// </summary>
class EvadeState : public BehaviorFighter::State {
public:
  explicit EvadeState(const Vector3 &direction) : direction_(direction) {}

protected:
  std::unique_ptr<State> OnUpdate(Enemy &enemy, const BehaviorFighter::Config &config,
                                  const BaseFrame &) override {
    const float kEvadeSpeed = 1.8f; // 回避の速さ（m/フレーム）
    enemy.GetTransform().translate += direction_ * kEvadeSpeed;

    // 配置の外側へ身を翻す
    const float kEvadeRoll = 0.5f; // 回避中のロール角（rad）
    const float rollSign = (enemy.GetSpawnOffset().x >= 0.0f) ? -1.0f : 1.0f;
    enemy.GetTransform().rotate.z = rollSign * kEvadeRoll;

    if (GetTimer() >= config.evadeDuration) {
      return std::make_unique<RetreatState>();
    }
    return nullptr;
  }

private:
  Vector3 direction_; // 回避する向き（長さ 1）
};

/// <summary>
/// 戦闘：配置の位置で上下にホバリングしながら自機を注視し、時間が来たら回避へ
/// </summary>
class CombatState : public BehaviorFighter::State {
protected:
  std::unique_ptr<State> OnUpdate(Enemy &enemy, const BehaviorFighter::Config &config,
                                  const BaseFrame &base) override {
    if (!enemy.GetPlayer()) {
      return nullptr;
    }
    const Vector3 &spawnOffset = enemy.GetSpawnOffset();

    // 生き物のような羽ばたき・ホバリング（上下sin波）
    const float floatOffset = std::sin(GetTimer() * config.hoverFrequency) * config.hoverAmplitude;
    enemy.GetTransform().translate = base.ToWorld(spawnOffset + Vector3{0.0f, floatOffset, 0.0f});

    FacePlayer(enemy);

    // 風に乗るような微小ロール（バンク）
    enemy.GetTransform().rotate.z = std::cos(GetTimer() * config.hoverFrequency) * config.rollAmplitude;

    if (GetTimer() >= config.combatDuration) {
      // 配置の位置に応じて外側の斜め上へ回避する
      const float kEvadeLift = 0.4f; // 回避の向きの上向きの割合
      const float evadeSign = (spawnOffset.x >= 0.0f) ? 1.0f : -1.0f;
      Vector3 direction = base.right * evadeSign + base.up * kEvadeLift;
      const float length = Length(direction);
      if (length > 0.001f) {
        direction = direction * (1.0f / length);
      }
      return std::make_unique<EvadeState>(direction);
    }
    return nullptr;
  }
};

/// <summary>
/// 登場：画面外から配置の位置へ減速しながら滑り込み、着いたら戦闘へ
/// </summary>
class EnterState : public BehaviorFighter::State {
protected:
  std::unique_ptr<State> OnUpdate(Enemy &enemy, const BehaviorFighter::Config &config,
                                  const BaseFrame &base) override {
    const Vector3 &targetOffset = enemy.GetSpawnOffset();
    // ターゲットが右寄りなら右画面外から、左寄りなら左画面外から進入
    const float enterSide = (targetOffset.x >= 0.0f) ? 1.0f : -1.0f;

    // 最初のフレームで、進入を始める画面外の位置を決める（少し上空・奥から滑り降りてくる）
    if (!startOffset_) {
      const float kStartHeight = 10.0f; // 配置の位置からの高さ（m）
      const float kStartDepth = 15.0f;  // 配置の位置からの奥行き（m）
      startOffset_ = targetOffset + Vector3{enterSide * config.enterOffsetDistance, kStartHeight, kStartDepth};
    }

    // Cubic Ease-Out（減速イージング）によるスムーズな滑り込み
    const float t = (std::min)(1.0f, GetTimer() / config.enterDuration);
    const float easeOutRatio = 1.0f - std::pow(1.0f - t, 3.0f);
    const Vector3 currentOffset = *startOffset_ + (targetOffset - *startOffset_) * easeOutRatio;
    enemy.GetTransform().translate = base.ToWorld(currentOffset);

    // 進入中のバンク角（進入方向に軽く傾き、着任時に水平に戻る）
    const float kEnterBank = 0.35f; // 進入を始めたときのロール角（rad）
    enemy.GetTransform().rotate.z = -enterSide * (1.0f - easeOutRatio) * kEnterBank;

    FacePlayer(enemy);

    if (GetTimer() >= config.enterDuration) {
      return std::make_unique<CombatState>();
    }
    return nullptr;
  }

private:
  std::optional<Vector3> startOffset_; // 進入を始める画面外の位置（配置の基準から見たオフセット）
};

} // namespace

BehaviorFighter::BehaviorFighter() : state_(std::make_unique<EnterState>()) {}

BehaviorFighter::BehaviorFighter(const Config& config)
    : state_(std::make_unique<EnterState>()), config_(config) {}

BehaviorFighter::~BehaviorFighter() = default;

void BehaviorFighter::Update(Enemy *enemy) {
  if (!enemy)
    return;

  const BaseFrame base{enemy->GetBasePosition(), enemy->GetBaseRight(),
                       enemy->GetBaseUp(), enemy->GetBaseForward()};
  if (std::unique_ptr<State> next = state_->Update(*enemy, config_, base)) {
    state_ = std::move(next);
  }
}
