#pragma once
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <memory>
#include <vector>

class Object3d;
class Object3dRenderer;

/// <summary>
/// 敵撃破時に飛び散る汎用ブロック破片（デブリ）演出
/// </summary>
class EnemyDebris : public BaseActor {
public:
  EnemyDebris(Object3dRenderer *renderer, const Vector3 &centerPos,
              const Vector3 &enemyScale, const Vector4 &color);
  ~EnemyDebris() override = default;

  void Initialize() override;
  void Update() override;
  void Draw3D() override;

private:
  struct Piece {
    std::unique_ptr<Object3d> model;
    Vector3 position;
    Vector3 velocity;
    Vector3 rotation;
    Vector3 angularVelocity;
    Vector3 baseScale;
  };

  std::vector<Piece> pieces_;
  float lifeTimer_ = 0.55f; // 0.55秒で消滅
  float maxLife_ = 0.55f;
};
