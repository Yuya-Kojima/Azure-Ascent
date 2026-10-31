#include "BehaviorTurret.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/ICamera.h"
#include "Actor/EnemyBullet.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include <cmath>
#include <algorithm>

BehaviorTurret::BehaviorTurret() : config_() {}

BehaviorTurret::BehaviorTurret(const Config& config) : config_(config) {}

void BehaviorTurret::Update(Enemy* enemy) {
  if (!enemy) return;

  auto camera = enemy->GetCamera();
  auto player = enemy->GetPlayer();
  if (!camera || !player) return;

  // 初期フレームで休眠時の静止姿勢（壁からの自然な向き）を記録
  if (!isInitialRotationSet_) {
    isInitialRotationSet_ = true;
    restRotation_ = enemy->GetTransform().rotate;
  }

  Vector3 playerPos = player->GetTransform().translate;
  Vector3 myPos = enemy->GetTransform().translate;

  // 自機との直線距離と、レールの進む向きに沿った前後の距離（正なら自機より前）を計算
  Vector3 d0 = {playerPos.x - myPos.x, playerPos.y - myPos.y, playerPos.z - myPos.z};
  float currentDist = std::sqrt(d0.x * d0.x + d0.y * d0.y + d0.z * d0.z);
  const Vector3& railForward = enemy->GetBaseForward();
  float forwardDistToPlayer = -(d0.x * railForward.x + d0.y * railForward.y + d0.z * railForward.z);

  // 1. 感知判定（Dormant -> Active への遷移）
  if (state_ == State::Dormant) {
    // 感知範囲（senseDistance）に入り、かつ自機の前方にいる場合に起動
    if (currentDist <= config_.senseDistance && forwardDistToPlayer > 0.0f) {
      state_ = State::Active;
      activeTimer_ = 0.0f;
      shotTimer_ = 0.0f;
    } else {
      // 休眠中は初期の静止回転を維持して沈黙
      enemy->GetTransform().rotate = restRotation_;
      return;
    }
  }

  // 起動後はタイマーを進行
  activeTimer_ += 1.0f / 60.0f;
  shotTimer_ += 1.0f / 60.0f;

  // 2. 自機の移動速度ベクトル（前フレームからの差分）を計測
  Vector3 playerVel = {0.0f, 0.0f, 0.0f};
  if (hasPrevPlayerPos_) {
    playerVel = {
        playerPos.x - prevPlayerPos_.x,
        playerPos.y - prevPlayerPos_.y,
        playerPos.z - prevPlayerPos_.z
    };
  }
  prevPlayerPos_ = playerPos;
  hasPrevPlayerPos_ = true;

  // 初回フレーム等で速度が取れていない場合のフォールバック（カメラ前進速度：秒速40m = 毎フレーム約0.67m）
  float velSq = playerVel.x * playerVel.x + playerVel.y * playerVel.y + playerVel.z * playerVel.z;
  if (velSq < 0.01f) {
    const Vector3& camFwd = enemy->GetBaseForward();
    playerVel = {camFwd.x * 0.67f, camFwd.y * 0.67f, camFwd.z * 0.67f};
  }

  // 未来予測位置（インターセプト）の物理計算
  // 2次方程式: (|V|^2 - s^2) * t^2 + 2*(d0 . V) * t + |d0|^2 = 0
  float a = (playerVel.x * playerVel.x + playerVel.y * playerVel.y + playerVel.z * playerVel.z) - (config_.bulletSpeed * config_.bulletSpeed);
  float b = 2.0f * (d0.x * playerVel.x + d0.y * playerVel.y + d0.z * playerVel.z);
  float c = d0.x * d0.x + d0.y * d0.y + d0.z * d0.z;

  float interceptTime = 0.0f;
  float disc = b * b - 4.0f * a * c;
  if (disc >= 0.0f && std::abs(a) > 0.0001f) {
    float sqrtDisc = std::sqrt(disc);
    float t1 = (-b - sqrtDisc) / (2.0f * a);
    float t2 = (-b + sqrtDisc) / (2.0f * a);
    if (t1 > 0.0f && t2 > 0.0f) {
      interceptTime = (std::min)(t1, t2);
    } else if (t1 > 0.0f) {
      interceptTime = t1;
    } else if (t2 > 0.0f) {
      interceptTime = t2;
    }
  }

  // 解が得られなかった場合のフォールバック（相対接近速度による近似）
  if (interceptTime <= 0.0f) {
    interceptTime = currentDist / (config_.bulletSpeed + 0.67f);
  }

  // 未来の自機到達位置
  Vector3 futurePlayerPos = {
      playerPos.x + playerVel.x * interceptTime,
      playerPos.y + playerVel.y * interceptTime,
      playerPos.z + playerVel.z * interceptTime
  };

  // 3. 照準（エイム）：自機が近すぎない限り未来位置を注視
  Vector3 aimDir = {futurePlayerPos.x - myPos.x, futurePlayerPos.y - myPos.y, futurePlayerPos.z - myPos.z};
  float aimDist = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y + aimDir.z * aimDir.z);

  if (aimDist > 0.001f && forwardDistToPlayer >= -5.0f) {
    enemy->GetTransform().rotate.y = std::atan2(aimDir.x, aimDir.z);
    enemy->GetTransform().rotate.x = -std::asin(std::clamp(aimDir.y / aimDist, -1.0f, 1.0f));
  }

  // 4. 射撃処理
  // すれ違い直前・通過後の理不尽な横殴り・背後射撃を防止するフェア設計
  bool isFairDistance = (forwardDistToPlayer >= config_.minFairDistance);
  bool isVisibleOnScreen = enemy->IsInScreen(0.1f);

  if (state_ == State::Active) {
    // 起動後、fireDelay（約1.0秒）の照準時間を経て初弾を発射
    if (activeTimer_ >= config_.fireDelay && isFairDistance && isVisibleOnScreen && aimDist > 0.001f) {
      state_ = State::Combat;
      shotTimer_ = 0.0f; // タイマーリセット

      Vector3 bulletVelocity = {
          (aimDir.x / aimDist) * config_.bulletSpeed,
          (aimDir.y / aimDist) * config_.bulletSpeed,
          (aimDir.z / aimDist) * config_.bulletSpeed
      };

      auto bullet = std::make_unique<EnemyBullet>();
      bullet->Initialize(
          PrefabManager::GetInstance()->GetObject3dRenderer(),
          myPos,
          bulletVelocity,
          const_cast<Player*>(player),
          EnemyBulletType::NormalDestructible
      );
      ActorManager::GetInstance()->AddActor(std::move(bullet));
    }
  } else if (state_ == State::Combat) {
    // 戦闘継続中：指定インターバルで追加射撃
    if (shotTimer_ >= config_.fireInterval && isFairDistance && isVisibleOnScreen && aimDist > 0.001f) {
      shotTimer_ = 0.0f;

      Vector3 bulletVelocity = {
          (aimDir.x / aimDist) * config_.bulletSpeed,
          (aimDir.y / aimDist) * config_.bulletSpeed,
          (aimDir.z / aimDist) * config_.bulletSpeed
      };

      auto bullet = std::make_unique<EnemyBullet>();
      bullet->Initialize(
          PrefabManager::GetInstance()->GetObject3dRenderer(),
          myPos,
          bulletVelocity,
          const_cast<Player*>(player),
          EnemyBulletType::NormalDestructible
      );
      ActorManager::GetInstance()->AddActor(std::move(bullet));
    }
  }
}
