#pragma once

// 自機の弾が当たる物。自機の弾（通常弾・ロックオン弾）が当たったときの受け方を決める
class IPlayerBulletHittable {
public:
  virtual ~IPlayerBulletHittable() = default;

  // 自機の弾が当たった。damage: 与えるダメージ、volleyId: その弾の射撃番号（通常弾なら kNoVolleyId）
  virtual void OnPlayerBulletHit(int damage, int volleyId) = 0;
};
