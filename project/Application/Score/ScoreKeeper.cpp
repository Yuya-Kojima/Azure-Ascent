#include "ScoreKeeper.h"
#include "ScoreConfig.h"
#include <algorithm>
#include <array>
#include <cmath>

ScoreKeeper::ScoreKeeper() : aliveToken_(std::make_shared<bool>(true)) {}

ScoreKeeper::~ScoreKeeper() = default;

void ScoreKeeper::Reset(const ScoreConfig *config, float bossSpawnTime) {
  config_ = config;
  bossSpawnTime_ = bossSpawnTime;
  // 射撃の番号は振り直さない（前のプレイの弾から届く知らせを、新しい射撃と取り違えない）
  volleys_.clear();
  enemies_.clear();
  breakdown_ = {};
  events_.clear();
}

int ScoreKeeper::BeginVolley(int bulletCount) {
  const int id = nextVolleyId_++;
  Volley volley;
  volley.pendingBullets = bulletCount;
  volleys_[id] = volley;
  return id;
}

void ScoreKeeper::FinishVolleyBullet(int volleyId) {
  auto it = volleys_.find(volleyId);
  if (it == volleys_.end()) {
    return;
  }
  Volley &volley = it->second;
  volley.pendingBullets--;
  if (volley.pendingBullets > 0) {
    return;
  }
  // 弾が全部終わったので、倒した数で点を確定する
  if (volley.kills > 0) {
    AddVolleyResult(volley.kills, volley.lastKillPosition);
  }
  volleys_.erase(it);
}

int ScoreKeeper::OnEnemySpawned(float spawnTime) {
  enemies_.push_back({spawnTime, false});
  return static_cast<int>(enemies_.size()) - 1;
}

void ScoreKeeper::OnEnemyKilled(int enemyId, int volleyId,
                                const Vector3 &position) {
  if (enemyId < 0 || enemyId >= static_cast<int>(enemies_.size()) ||
      enemies_[enemyId].isKilled || !config_) {
    return;
  }
  enemies_[enemyId].isKilled = true;
  breakdown_.kills++;
  breakdown_.killPoints += config_->pointPerKill;

  // 弾が飛んでいる射撃で倒したなら、その射撃の数に足す。確定済みの射撃や通常弾なら 1 体の射撃として確定する
  auto it = volleys_.find(volleyId);
  if (it != volleys_.end()) {
    it->second.kills++;
    it->second.lastKillPosition = position;
  } else {
    AddVolleyResult(1, position);
  }
}

void ScoreKeeper::OnBossKilled() {
  if (config_) {
    breakdown_.bossPoints = config_->bossPoint;
  }
}

void ScoreKeeper::FinishAllVolleys() {
  for (auto &[id, volley] : volleys_) {
    if (volley.kills > 0) {
      AddVolleyResult(volley.kills, volley.lastKillPosition);
    }
  }
  volleys_.clear();
}

std::vector<ScoreKeeper::ScoreEvent> ScoreKeeper::ConsumeEvents() {
  std::vector<ScoreEvent> events;
  events.swap(events_);
  return events;
}

float ScoreKeeper::GetKillRate() const {
  if (enemies_.empty()) {
    return 1.0f;
  }
  return static_cast<float>(breakdown_.kills) /
         static_cast<float>(enemies_.size());
}

int ScoreKeeper::GetWorstSection() const {
  std::array<int, kSectionCount> missed{};
  for (const EnemyEntry &enemy : enemies_) {
    if (enemy.isKilled) {
      continue;
    }
    // 出撃 〜 ボスの道のりのどこで出てきたか（0〜1）を、区間に振り分ける
    const float progress =
        bossSpawnTime_ > 0.0f
            ? std::clamp(enemy.spawnTime / bossSpawnTime_, 0.0f, 1.0f)
            : 0.0f;
    const int section = (std::min)(
        static_cast<int>(progress * static_cast<float>(kSectionCount)),
        kSectionCount - 1);
    missed[section]++;
  }
  int worst = -1;
  int worstCount = 0;
  for (int i = 0; i < kSectionCount; ++i) {
    if (missed[i] > worstCount) {
      worst = i;
      worstCount = missed[i];
    }
  }
  return worst;
}

void ScoreKeeper::AddVolleyResult(int kills, const Vector3 &position) {
  const int base = kills * config_->pointPerKill;
  int points = kills * kills * config_->pointPerKill;
  if (kills >= kMaxLockKills) {
    points = static_cast<int>(std::lround(static_cast<float>(points) *
                                          config_->maxLockMultiplier));
    breakdown_.maxLockCount++;
    breakdown_.maxLockPoints += points - base;
  } else {
    breakdown_.volleyPoints += points - base;
  }
  events_.push_back({position, points, kills});
}
