#pragma once
#include "Score/ScoreConfig.h"
#include <string>

/// <summary>
/// 空域ごとの記録（save/records.json に、レベルのファイル名ごとに書く）
/// ベストのスコア・ランク・撃墜率はそれぞれの最高（同じプレイの値とは限らない）
/// </summary>
struct StageRecord {
  bool hasRecord = false;     // 一度でもクリアしたか
  int bestScore = 0;
  Rank bestRank = Rank::C;    // ベストの総合ランク
  float bestKillRate = 0.0f;  // ベストの撃墜率（0〜1）

  /// <summary>
  /// 空域の記録を読む（なければ hasRecord が false）
  /// </summary>
  static StageRecord Load(const std::string &stageName);

  /// <summary>
  /// 空域の記録を書く（ほかの空域の記録はそのまま残す）
  /// </summary>
  static void Save(const std::string &stageName, const StageRecord &record);
};
