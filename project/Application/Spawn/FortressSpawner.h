#pragma once
#include "Spawn/EnemySpawner.h"
#include <string>

/// <summary>
/// 浮かぶ砦の出し方。砦と部品 8 個を作り、部品を出た敵として数える
/// </summary>
class FortressSpawner : public EnemySpawner {
public:
  SpawnResult Spawn(const SpawnEvent &ev, const SpawnContext &context) override;

private:
  static inline const std::string kPartModelPath_ = "__builtin_box"; // 部品のモデル（エンジン組み込みの一辺 1 の箱）
  static constexpr Vector4 kPartColor_ = {1.0f, 0.2f, 0.2f, 1.0f}; // 部品の色（ふだんの敵と同じ赤）
};
