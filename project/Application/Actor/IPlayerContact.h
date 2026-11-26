#pragma once

// 自機にぶつかってダメージを与えられる物。当たれるかと、当たったときの受け方を決める
class IPlayerContact {
public:
  virtual ~IPlayerContact() = default;

  // 今ぶつかったら自機にダメージを与えるか
  virtual bool CanHitPlayer() const = 0;

  // 自機に当たり、自機がダメージを受けた
  virtual void OnHitPlayer() = 0;
};
