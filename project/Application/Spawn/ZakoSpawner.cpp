#include "Spawn/ZakoSpawner.h"
#include "Actor/Behavior/BehaviorFighter.h"
#include "Actor/Behavior/BehaviorMeteor.h"
#include "Actor/Behavior/BehaviorSineWave.h"
#include "Actor/Behavior/BehaviorSpline.h"
#include "Actor/Behavior/BehaviorStationary.h"
#include "Actor/Behavior/BehaviorStrafe.h"
#include "Actor/Behavior/BehaviorStraight.h"
#include "Actor/Behavior/BehaviorTurret.h"
#include "Framework/PrefabManager.h"

SpawnResult ZakoSpawner::Spawn(const SpawnEvent &ev,
                               const SpawnContext &context) {
  auto enemy = PrefabManager::GetInstance()->InstantiateEnemy(
      ev.prefabName,
      Transform{{kSpawnScale_, kSpawnScale_, kSpawnScale_},
                {0.0f, 0.0f, 0.0f},
                CalcSpawnPosition(ev, *context.camera)});

  enemy->SetMoveType(ev.moveType);
  enemy->SetMoveDirection(ev.moveDirection);
  enemy->SetBehavior(CreateBehavior(ev.moveType));

  // 道筋の名前があれば、その道筋に沿って動く
  if (!ev.splineName.empty()) {
    auto spline = context.splines->find(ev.splineName);
    if (spline != context.splines->end()) {
      enemy->SetBehavior(std::make_unique<BehaviorSpline>(
          spline->second, ev.splineDuration, ev.isWorldSpaceSpline,
          ev.fireInterval));
    }
  }

  enemy->SetCamera(context.camera);
  enemy->SetPlayer(context.player);
  enemy->SetSpawnOffset(ev.spawnOffset);

  // 出た敵として数え、自機の弾で倒したときだけ撃破に数える
  context.countForScore(enemy.get(), ev.spawnTime);

  SpawnResult result;
  result.enemies.push_back(std::move(enemy));
  return result;
}

std::unique_ptr<IEnemyBehavior> ZakoSpawner::CreateBehavior(MoveType moveType) {
  switch (moveType) {
  case MoveType::SineWave:
    return std::make_unique<BehaviorSineWave>();
  case MoveType::Stationary:
    return std::make_unique<BehaviorStationary>();
  case MoveType::Fighter:
    return std::make_unique<BehaviorFighter>();
  case MoveType::Meteor:
    return std::make_unique<BehaviorMeteor>();
  case MoveType::Strafe:
    return std::make_unique<BehaviorStrafe>();
  case MoveType::Turret:
    return std::make_unique<BehaviorTurret>();
  default:
    // Straight と Parallel は直進
    return std::make_unique<BehaviorStraight>();
  }
}
