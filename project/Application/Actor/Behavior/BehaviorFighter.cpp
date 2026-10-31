#include "BehaviorFighter.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>

BehaviorFighter::BehaviorFighter() : state_(State::Enter), stateTimer_(0.0f) {}

BehaviorFighter::BehaviorFighter(const Config& config)
    : state_(State::Enter), stateTimer_(0.0f), config_(config) {}

void BehaviorFighter::Update(Enemy *enemy) {
  if (!enemy)
    return;

  stateTimer_ += 1.0f / 60.0f;

  const Vector3& basePos = enemy->GetBasePosition();
  const Vector3& baseRight = enemy->GetBaseRight();
  const Vector3& baseUp = enemy->GetBaseUp();
  const Vector3& baseForward = enemy->GetBaseForward();

  switch (state_) {
  case State::Enter:
    UpdateEnter(enemy, basePos, baseRight, baseUp, baseForward);
    break;
  case State::Combat:
    UpdateCombat(enemy, basePos, baseRight, baseUp, baseForward);
    break;
  case State::Evade:
    UpdateEvade(enemy, basePos, baseRight, baseUp, baseForward);
    break;
  case State::Retreat:
    UpdateRetreat(enemy, basePos, baseRight, baseUp, baseForward);
    break;
  }
}

void BehaviorFighter::UpdateEnter(Enemy *enemy, const Vector3& cameraPos, const Vector3& cameraRight, const Vector3& cameraUp, const Vector3& cameraForward) {
  const Vector3 &targetOffset = enemy->GetSpawnOffset();

  // 初回フレームで進入開始時の画面外初期オフセットを計算
  if (!isEnterInitialized_) {
    isEnterInitialized_ = true;
    // ターゲットが右寄りなら右画面外から、左寄りなら左画面外から進入
    float enterSide = (targetOffset.x >= 0.0f) ? 1.0f : -1.0f;
    startOffset_ = {
      targetOffset.x + enterSide * config_.enterOffsetDistance,
      targetOffset.y + 10.0f, // 少し上空から滑り降りてくる
      targetOffset.z + 15.0f
    };
  }

  // Cubic Ease-Out（減速イージング）によるスムーズな滑り込み
  float t = (std::min)(1.0f, stateTimer_ / config_.enterDuration);
  float easeOutRatio = 1.0f - std::pow(1.0f - t, 3.0f);

  Vector3 currentOffset = {
    startOffset_.x + (targetOffset.x - startOffset_.x) * easeOutRatio,
    startOffset_.y + (targetOffset.y - startOffset_.y) * easeOutRatio,
    startOffset_.z + (targetOffset.z - startOffset_.z) * easeOutRatio
  };

  enemy->GetTransform().translate =
      cameraPos +
      Vector3{cameraRight.x * currentOffset.x, cameraRight.y * currentOffset.x, cameraRight.z * currentOffset.x} +
      Vector3{cameraUp.x * currentOffset.y, cameraUp.y * currentOffset.y, cameraUp.z * currentOffset.y} +
      Vector3{cameraForward.x * currentOffset.z, cameraForward.y * currentOffset.z, cameraForward.z * currentOffset.z};

  // 進入中のバンク角（進入方向に軽く傾き、着任時に水平に戻る）
  float enterSide = (targetOffset.x >= 0.0f) ? 1.0f : -1.0f;
  float rollBank = -enterSide * (1.0f - easeOutRatio) * 0.35f;
  enemy->GetTransform().rotate.z = rollBank;

  // 機首はプレイヤーまたは前方に向ける
  if (auto player = enemy->GetPlayer()) {
    Vector3 playerPos = player->GetTransform().translate;
    Vector3 dirToPlayer = {playerPos.x - enemy->GetTransform().translate.x,
                           playerPos.y - enemy->GetTransform().translate.y,
                           playerPos.z - enemy->GetTransform().translate.z};
    enemy->GetTransform().rotate.y = std::atan2(dirToPlayer.x, dirToPlayer.z);
  }

  if (stateTimer_ >= config_.enterDuration) {
    state_ = State::Combat;
    stateTimer_ = 0.0f;
  }
}

void BehaviorFighter::UpdateCombat(Enemy *enemy, const Vector3& cameraPos, const Vector3& cameraRight, const Vector3& cameraUp, const Vector3& cameraForward) {
  auto player = enemy->GetPlayer();
  if (!player)
    return;

  const Vector3 &spawnOffset = enemy->GetSpawnOffset();

  // 生き物のような羽ばたき・ホバリング（上下sin波）
  float floatOffset = std::sin(stateTimer_ * config_.hoverFrequency) * config_.hoverAmplitude;

  enemy->GetTransform().translate =
      cameraPos +
      Vector3{cameraRight.x * spawnOffset.x, cameraRight.y * spawnOffset.x, cameraRight.z * spawnOffset.x} +
      Vector3{cameraUp.x * (spawnOffset.y + floatOffset), cameraUp.y * (spawnOffset.y + floatOffset), cameraUp.z * (spawnOffset.y + floatOffset)} +
      Vector3{cameraForward.x * spawnOffset.z, cameraForward.y * spawnOffset.z, cameraForward.z * spawnOffset.z};

  // プレイヤーを注視する（照準を合わせている感覚を演出）
  Vector3 playerPos = player->GetTransform().translate;
  Vector3 dirToPlayer = {playerPos.x - enemy->GetTransform().translate.x,
                         playerPos.y - enemy->GetTransform().translate.y,
                         playerPos.z - enemy->GetTransform().translate.z};
  enemy->GetTransform().rotate.y = std::atan2(dirToPlayer.x, dirToPlayer.z);

  // 風に乗るような微小ロール（バンク）
  float swayRoll = std::cos(stateTimer_ * config_.hoverFrequency) * config_.rollAmplitude;
  enemy->GetTransform().rotate.z = swayRoll;

  if (stateTimer_ >= config_.combatDuration) {
    state_ = State::Evade;
    stateTimer_ = 0.0f;
    // 回避方向を決定（配置位置に応じて外側へ回避）
    float evadeSign = (spawnOffset.x >= 0.0f) ? 1.0f : -1.0f;
    evadeDir_ = Vector3{cameraRight.x * evadeSign + cameraUp.x * 0.4f,
                        cameraRight.y * evadeSign + cameraUp.y * 0.4f,
                        cameraRight.z * evadeSign + cameraUp.z * 0.4f};
    float len = std::sqrt(evadeDir_.x * evadeDir_.x + evadeDir_.y * evadeDir_.y + evadeDir_.z * evadeDir_.z);
    if (len > 0.001f) {
      evadeDir_.x /= len;
      evadeDir_.y /= len;
      evadeDir_.z /= len;
    }
  }
}

void BehaviorFighter::UpdateEvade(Enemy *enemy, const Vector3& cameraPos, const Vector3& cameraRight, const Vector3& cameraUp, const Vector3& cameraForward) {
  // 決定した回避方向へ急加速（ロールを深めて身を翻す）
  float evadeSpeed = 1.8f;
  enemy->GetTransform().translate.x += evadeDir_.x * evadeSpeed;
  enemy->GetTransform().translate.y += evadeDir_.y * evadeSpeed;
  enemy->GetTransform().translate.z += evadeDir_.z * evadeSpeed;

  float rollSign = (enemy->GetSpawnOffset().x >= 0.0f) ? -1.0f : 1.0f;
  enemy->GetTransform().rotate.z = rollSign * 0.5f;

  if (stateTimer_ >= config_.evadeDuration) {
    state_ = State::Retreat;
    stateTimer_ = 0.0f;
  }
}

void BehaviorFighter::UpdateRetreat(Enemy *enemy, const Vector3& cameraPos, const Vector3& cameraRight, const Vector3& cameraUp, const Vector3& cameraForward) {
  // 画面奥（前方）へ飛び去る
  float retreatSpeed = 2.5f;
  enemy->GetTransform().translate.x += cameraForward.x * retreatSpeed;
  enemy->GetTransform().translate.y += cameraForward.y * retreatSpeed + 0.3f;
  enemy->GetTransform().translate.z += cameraForward.z * retreatSpeed;

  // 画面の外に 1 秒続けていたら消滅
  if (stateTimer_ > 0.5f && enemy->HasLeftScreen()) {
    enemy->Destroy();
  } else if (stateTimer_ > 5.0f) {
    enemy->Destroy(); // 安全マージン
  }
}
