#pragma once
#include "Math/Vector3.h"
#include "Score/VolleyId.h"
#include <memory>
#include <unordered_map>
#include <vector>

class ScoreConfig;

/// <summary>
/// 1 回のプレイの点と撃墜率を数える
/// ロックオンの一斉射撃に番号を振り、その射撃の弾が全部「当たった／狙った敵を失った」になった時点で、
/// 倒した数 n から点を確定する（n の二乗 × 1 体の点。最大ロックの 8 体はさらに倍率を掛ける）
/// 出てきた敵には番号を振り、出たときのレールの進み具合を覚えて、逃した区間を数える
/// </summary>
class ScoreKeeper {
public:
  // 最大ロックになる、1 回の射撃で倒した数
  static constexpr int kMaxLockKills = 8;
  // 逃した敵を数える区間の数（出撃 〜 ボスを等分した 序盤・中盤・終盤）
  static constexpr int kSectionCount = 3;

  /// <summary>
  /// 点の内訳（足すとスコアになる）
  /// </summary>
  struct Breakdown {
    int kills = 0;         // 倒した数
    int killPoints = 0;    // 撃破: 倒した数 × 1 体の点
    int volleyPoints = 0;  // まとめ撃ち: 2〜7 体の射撃で、撃破の点に上乗せした分
    int maxLockCount = 0;  // 最大ロックの回数
    int maxLockPoints = 0; // 最大ロック: 8 体の射撃で、撃破の点に上乗せした分
    int bossPoints = 0;    // ボス撃破
    int GetTotal() const {
      return killPoints + volleyPoints + maxLockPoints + bossPoints;
    }
  };

  /// <summary>
  /// 点が決まった出来事（HUD の得点の文字に使う）
  /// </summary>
  struct ScoreEvent {
    Vector3 position{}; // 最後に倒した敵の位置
    int points = 0;     // その射撃の点
    int kills = 0;      // その射撃で倒した数
  };

  ScoreKeeper();
  ~ScoreKeeper();

  /// <summary>
  /// プレイの始めに数えた物を消す
  /// </summary>
  /// <param name="config">点の入り方</param>
  /// <param name="bossSpawnTime">ボスが出るレールの進み具合（区間を分ける道のりの終わり）</param>
  void Reset(const ScoreConfig *config, float bossSpawnTime);

  /// <summary>
  /// 一斉射撃を始める。射撃の番号を返す
  /// </summary>
  /// <param name="bulletCount">この射撃で出す弾の数</param>
  int BeginVolley(int bulletCount);

  /// <summary>
  /// 一斉射撃の弾が 1 発、当たった・狙った敵を失った・出ずに捨てられた
  /// </summary>
  void FinishVolleyBullet(int volleyId);

  /// <summary>
  /// 敵が出てきた（ボスは除く）。敵の番号を返す
  /// </summary>
  /// <param name="spawnTime">出てきたレールの進み具合</param>
  int OnEnemySpawned(float spawnTime);

  /// <summary>
  /// 自機の弾で敵を倒した
  /// </summary>
  /// <param name="enemyId">OnEnemySpawned が返した番号</param>
  /// <param name="volleyId">最後に当たった弾の射撃番号（通常弾なら kNoVolleyId）</param>
  /// <param name="position">倒した位置</param>
  void OnEnemyKilled(int enemyId, int volleyId, const Vector3 &position);

  /// <summary>
  /// ボスを倒した
  /// </summary>
  void OnBossKilled();

  /// <summary>
  /// まだ弾が飛んでいる射撃を、今の倒した数で確定する（クリア時）
  /// </summary>
  void FinishAllVolleys();

  /// <summary>
  /// 前回呼んでから点が決まった出来事（返したら消す）
  /// </summary>
  std::vector<ScoreEvent> ConsumeEvents();

  const Breakdown &GetBreakdown() const { return breakdown_; }
  int GetScore() const { return breakdown_.GetTotal(); }
  int GetSpawnedCount() const { return static_cast<int>(enemies_.size()); }
  int GetKilledCount() const { return breakdown_.kills; }

  /// <summary>
  /// 撃墜率（0〜1。敵が出てこなければ 1）
  /// </summary>
  float GetKillRate() const;

  /// <summary>
  /// いちばん多く逃した区間（0: 序盤 〜 kSectionCount - 1: 終盤。逃していなければ -1。同じ数なら手前の区間）
  /// </summary>
  int GetWorstSection() const;

  /// <summary>
  /// 弾のコールバックがこの数える係を呼んでよいかを確かめる印（この係が消えると切れる）
  /// </summary>
  std::weak_ptr<bool> GetAliveToken() const { return aliveToken_; }

private:
  // 一斉射撃
  struct Volley {
    int pendingBullets = 0; // まだ終わっていない弾の数
    int kills = 0;          // 倒した数
    Vector3 lastKillPosition{};
  };

  // 出てきた敵
  struct EnemyEntry {
    float spawnTime = 0.0f;
    bool isKilled = false;
  };

  // 1 回の射撃で n 体倒したときの点を内訳に足し、出来事を残す
  void AddVolleyResult(int kills, const Vector3 &position);

  const ScoreConfig *config_ = nullptr;
  float bossSpawnTime_ = 0.0f;
  std::unordered_map<int, Volley> volleys_;
  int nextVolleyId_ = 0;
  std::vector<EnemyEntry> enemies_;
  Breakdown breakdown_;
  std::vector<ScoreEvent> events_;
  std::shared_ptr<bool> aliveToken_;
};
