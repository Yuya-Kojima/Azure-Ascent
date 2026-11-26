#pragma once
#include "Spawn/EnemySpawner.h"

/// <summary>
/// ボスの出し方。ボスを作って体力バーを用意する（BGM・レール・クリア演出は受け取ったシーンが変える）
/// </summary>
class BossSpawner : public EnemySpawner {
public:
  SpawnResult Spawn(const SpawnEvent &ev, const SpawnContext &context) override;

private:
  static constexpr float kBossScale_ = 10.0f; // ボスの大きさ（プレハブの大きさより優先する）
};
