#pragma once

// ロックオンの相手になれる物。ロックオンで狙えるかを決める
class ILockOnHittable {
public:
  virtual ~ILockOnHittable() = default;

  // 今ロックオンできるか
  virtual bool CanBeLockedOn() const = 0;
};
