#pragma once
#include "Actor/Enemy.h"

class Boss;

class BossCore : public Enemy {
public:
  BossCore() = default;
  ~BossCore() override = default;

  void Initialize() override;
  void Update() override;

  void TakeDamage(int damage, bool isSelfDestruct = false) override;
  bool CanBeLockedOn() const override { return lockable_; }

  void SetBoss(Boss* boss) { boss_ = boss; }
  void SetOffset(const Vector3& offset) { offset_ = offset; }
  
  // 自身を覆う装甲（シールド）をセットする
  void SetShield(class BossBit* shield) { shield_ = shield; }

private:
  Boss* boss_ = nullptr;
  class BossBit* shield_ = nullptr;
  Vector3 offset_{0.0f, 0.0f, 0.0f};
  bool lockable_ = false; // ロックオンできるか（装甲が全部壊れている間だけ true）
};
