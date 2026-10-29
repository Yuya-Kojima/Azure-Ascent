#pragma once
#include <array>

/// <summary>
/// 評価のランク（低い順）
/// </summary>
enum class Rank { C, B, A, S };

/// <summary>
/// 評価の調整値（resources/config/ScoreConfig.json）
/// 点の入り方と、スコア・撃墜率それぞれのランクの基準
/// </summary>
class ScoreConfig {
public:
  // 基準を持つランクの数（S・A・B。どれにも届かなければ C）
  static constexpr int kRankBorderCount = 3;

  /// <summary>
  /// ファイルから読む（ファイルにない値は今の値のまま）
  /// </summary>
  void Load();

  /// <summary>
  /// ファイルへ書き出す
  /// </summary>
  void Save() const;

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// スコアのランク
  /// </summary>
  Rank GetScoreRank(int score) const;

  /// <summary>
  /// 撃墜率（0〜1）のランク
  /// </summary>
  Rank GetKillRateRank(float killRate) const;

  /// <summary>
  /// 総合ランク（2 つのランクの低いほう）
  /// </summary>
  static Rank GetOverallRank(Rank scoreRank, Rank killRateRank);

  /// <summary>
  /// rank を取るのに要るスコア（C なら 0）
  /// </summary>
  int GetScoreBorder(Rank rank) const;

  /// <summary>
  /// rank を取るのに要る撃墜率（0〜1。C なら 0）
  /// </summary>
  float GetKillRateBorder(Rank rank) const;

  int pointPerKill = 100;         // 1 体の点
  float maxLockMultiplier = 2.0f; // 最大ロック（8 体）の射撃に掛ける倍率
  int bossPoint = 10000;          // ボス撃破の点
  // スコアの基準（S, A, B の順）
  std::array<int, kRankBorderCount> scoreBorders = {50000, 30000, 15000};
  // 撃墜率の基準（0〜1。S, A, B の順）
  std::array<float, kRankBorderCount> killRateBorders = {0.9f, 0.75f, 0.5f};
};
