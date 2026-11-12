#pragma once

// ロックオンの相手になれる物。ロックオンで狙えるかと、ロックオン弾が着いたときの受け方を決める
class ILockOnHittable {
public:
  virtual ~ILockOnHittable() = default;

  // 今ロックオンできるか
  virtual bool CanBeLockedOn() const = 0;

  // ロックオン弾が着いた。damage: 与えるダメージ、volleyId: その弾の射撃番号
  virtual void OnLockOnHit(int damage, int volleyId) = 0;
};
