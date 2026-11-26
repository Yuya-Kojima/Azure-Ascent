#include "Spawn/BossSpawner.h"
#include "Actor/Boss.h"
#include "Framework/PrefabManager.h"

SpawnResult BossSpawner::Spawn(const SpawnEvent &ev,
                               const SpawnContext &context) {
  auto boss = std::make_unique<Boss>();
  PrefabManager::GetInstance()->InitializeEnemy(
      *boss, ev.prefabName,
      Transform{{kBossScale_, kBossScale_, kBossScale_},
                {0.0f, 0.0f, 0.0f},
                CalcSpawnPosition(ev, *context.camera)});
  boss->GetTransform().scale = {kBossScale_, kBossScale_, kBossScale_};

  boss->InitializeUI(context.spriteRenderer);
  boss->SetCamera(context.camera);
  boss->SetPlayer(context.player);
  boss->SetSpawnOffset(ev.spawnOffset);

  // ボスは出た敵として数えない（最後の一撃で数える）
  SpawnResult result;
  result.boss = boss.get();
  result.enemies.push_back(std::move(boss));
  return result;
}
