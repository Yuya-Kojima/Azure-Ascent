#pragma once
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include <memory>
#include <vector>

class Object3d;
class Object3dRenderer;
class SphereCollider;
class Collider;
class Model;

class NormalBullet : public BaseActor {
public:
  NormalBullet();
  ~NormalBullet() override;

  /// <summary>
  /// 通常弾の初期化
  /// </summary>
  void Initialize(Object3dRenderer* renderer, const Vector3& startPos, const Vector3& velocity);
  
  void Update() override;
  void Draw3D() override;
  
  void OnCollision(Collider* other) override;

private:
  // 入れ物へしまう前に、当たり判定を外す
  void OnReturnToPool() override;

  std::unique_ptr<Object3d> object3d_;
  std::unique_ptr<SphereCollider> collider_;
  
  Vector3 velocity_;
  int damage_ = 1;
  static constexpr int kEnemyBulletDamage_ = 1; // 敵の弾に当たったときに与えるダメージ
  int lifeTimer_ = 60; // 寿命（約1秒で消滅）
};
