#include "BehaviorTurret.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include "Math/MathUtil.h"
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
    // 自機の前方にいて、画面に映り、カメラから見て箱の陰にいない場合に起動
    if (forwardDistToPlayer > 0.0f && enemy->IsInScreen() && !player->IsHiddenFromCamera(myPos)) {
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

  // 2. 弾が届くころの自機の位置（レールに沿って先回りした位置）を求める
  Vector3 futurePlayerPos = PredictPlayerPos(dynamic_cast<const RailCamera*>(camera), myPos, playerPos, config_.bulletSpeed);

  // 3. 照準（エイム）：自機が近すぎない限り未来位置を注視
  Vector3 aimDir = {futurePlayerPos.x - myPos.x, futurePlayerPos.y - myPos.y, futurePlayerPos.z - myPos.z};
  float aimDist = std::sqrt(aimDir.x * aimDir.x + aimDir.y * aimDir.y + aimDir.z * aimDir.z);

  if (aimDist > 0.001f && forwardDistToPlayer >= -5.0f) {
    enemy->GetTransform().rotate.y = std::atan2(aimDir.x, aimDir.z);
    enemy->GetTransform().rotate.x = -std::asin(std::clamp(aimDir.y / aimDist, -1.0f, 1.0f));
  }

  // 4. 射撃処理
  // 自機より十分前にいて、弾が消えない距離にいるときだけ撃つ
  bool isFairDistance = (forwardDistToPlayer >= config_.minFairDistance) && (currentDist <= config_.maxShotDistance);
  bool isVisibleOnScreen = enemy->IsInScreen(0.1f);

  if (state_ == State::Active) {
    // 起動後、fireDelay の照準時間を経て初弾を発射
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

Vector3 BehaviorTurret::PredictPlayerPos(const RailCamera* rail, const Vector3& myPos, const Vector3& playerPos, float bulletSpeed) {
  if (!rail || bulletSpeed <= 0.0f) {
    return playerPos;
  }

  // 自機の、レールの基準から見た今のずれ（右・上・前の成分）
  const Vector3 offset = playerPos - rail->GetRailPosition();
  const float offRight = Dot(offset, rail->GetRailRight());
  const float offUp = Dot(offset, rail->GetRailUp());
  const float offForward = Dot(offset, rail->GetRailForward());

  const float nowT = rail->GetT();
  const float maxT = static_cast<float>(rail->GetWaypoints().size() - 1);
  const float tPerFrame = rail->GetCurrentSpeed() / 60.0f; // 1 フレームに進む区間の数
  const Vector3 worldUp = {0.0f, 1.0f, 0.0f};

  // 届く時間（フレーム）を仮に決めて、その時の自機の位置までの距離から届く時間を求め直す。
  // 弾が自機より速いので、くり返すと届く時間は 1 つの値に近づく
  Vector3 future = playerPos;
  float frames = Length(playerPos - myPos) / bulletSpeed;
  for (int i = 0; i < kPredictIterations_; ++i) {
    const float t = (std::min)(nowT + tPerFrame * frames, maxT);
    // RailCamera::Update と同じ作り方で、その t のレールの位置と向きを出す
    Vector3 railPos = rail->CalcPosition(t);
    railPos.y += RailCamera::kRailHeight_;
    const Vector3 forward = rail->CalcTangent(t);
    const Vector3 right = SafeNormalize(Cross(worldUp, forward));
    const Vector3 up = SafeNormalize(Cross(forward, right));
    future = railPos + right * offRight + up * offUp + forward * offForward;
    frames = Length(future - myPos) / bulletSpeed;
  }
  return future;
}
