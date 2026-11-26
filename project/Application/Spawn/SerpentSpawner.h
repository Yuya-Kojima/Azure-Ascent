#pragma once
#include "Spawn/EnemySpawner.h"
#include <string>

/// <summary>
/// 大蛇の出し方。大蛇の 1 回の出番と光る節を作り、光る節を出た敵として数える
/// </summary>
class SerpentSpawner : public EnemySpawner {
public:
  SpawnResult Spawn(const SpawnEvent &ev, const SpawnContext &context) override;

private:
  static inline const std::string kPartModelPath_ = "__builtin_box"; // 光る節のモデル（エンジン組み込みの一辺 1 の箱）
};
