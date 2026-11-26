#pragma once
#include "Spawn/EnemySpawner.h"

/// <summary>
/// 雑魚敵の出し方。プレハブから 1 体作り、動き方を付けて、出た敵として数える
/// </summary>
class ZakoSpawner : public EnemySpawner {
public:
  SpawnResult Spawn(const SpawnEvent &ev, const SpawnContext &context) override;

private:
  // 動きの種類 moveType の動き方を作る
  static std::unique_ptr<IEnemyBehavior> CreateBehavior(MoveType moveType);

  static constexpr float kSpawnScale_ = 3.0f; // プレハブに大きさがないときの大きさ
};
