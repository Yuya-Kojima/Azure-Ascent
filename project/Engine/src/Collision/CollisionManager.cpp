#include "Collision/CollisionManager.h"
#include "Collision/Collider.h"
#include "Collision/SphereCollider.h"
#include "Math/CollisionMath.h"
#include "Math/MathUtil.h"

CollisionManager *CollisionManager::GetInstance() {
  static CollisionManager instance;
  return &instance;
}

void CollisionManager::Initialize() { colliders_.clear(); }

void CollisionManager::Register(Collider *collider) {
  colliders_.push_back(collider);
}

void CollisionManager::Remove(Collider *collider) {
  colliders_.remove(collider);
}

void CollisionManager::Clear() { colliders_.clear(); }

void CollisionManager::Update() {
  // 全コライダーの座標を更新
  for (Collider *collider : colliders_) {
    if (!collider->IsEnable()) continue;
    collider->Update();
  }

  // 総当り判定
  CheckAllCollisions();
}

void CollisionManager::DrawDebug() {
  if (!isDebugDrawEnabled_) return;
  for (Collider *collider : colliders_) {
    if (!collider->IsEnable()) continue;
    collider->DrawDebug();
  }
}

void CollisionManager::CheckAllCollisions() {
  auto itA = colliders_.begin();
  for (; itA != colliders_.end(); ++itA) {
    Collider *colliderA = *itA;
    if (!colliderA->IsEnable()) continue;

    auto itB = itA;
    ++itB;
    for (; itB != colliders_.end(); ++itB) {
      Collider *colliderB = *itB;
      if (!colliderB->IsEnable()) continue;

      // 属性とマスクを使ったフィルタリング
      // Aの属性がBのマスクに含まれていない、またはBの属性がAのマスクに含まれていない場合は計算スキップ
      if (!(colliderA->GetAttribute() & colliderB->GetMask()) ||
          !(colliderB->GetAttribute() & colliderA->GetMask())) {
        continue;
      }

      // 形状ごとの判定
      bool isHit = false;

      // 球と球 の判定
      if (colliderA->GetShapeType() == Collider::ShapeType::Sphere &&
          colliderB->GetShapeType() == Collider::ShapeType::Sphere) {

        SphereCollider *sphereA = static_cast<SphereCollider *>(colliderA);
        SphereCollider *sphereB = static_cast<SphereCollider *>(colliderB);

        const Vector3& velA = sphereA->GetVelocity();
        const Vector3& velB = sphereB->GetVelocity();

          if (LengthSq(velA) > 0.0f || LengthSq(velB) > 0.0f) {
              // 双方が速度を持つ場合を考慮して、相対速度でSwept Sphere判定を行う
              // Aを基準点（静止）とし、Bが相対速度で移動したとみなす
              Vector3 relativeVel = Subtract(velB, velA);
              
              Segment seg;
              Vector3 currentPosB = sphereB->GetWorldSphere().center;
              
              // 1フレーム前の位置から今の位置までの線分
              seg.origin = Subtract(currentPosB, relativeVel);
              seg.diff = relativeVel;
  
              Sphere expandedSphereA = sphereA->GetWorldSphere();
              // 移動する球(B)の半径を加算して太さを考慮する
              expandedSphereA.radius += sphereB->GetWorldSphere().radius;
  
              isHit = CollisionMath::IsCollision(seg, expandedSphereA);
          } else {
            // 静的（離散的）な球×球判定
            isHit = CollisionMath::IsCollision(sphereA->GetWorldSphere(),
                                               sphereB->GetWorldSphere());
        }
      }
      // ※新しくAABB やOBBなどのコライダーが増えたら、ここに分岐を追加する

      // 当たっていたらお互いのコールバックを呼ぶ
      if (isHit) {
        colliderA->OnCollision(colliderB);
        colliderB->OnCollision(colliderA);
      }
    }
  }
}

bool CollisionManager::Raycast(const Ray& ray, uint32_t mask, Collider** outCollider, float* outDistance) {
  bool hit = false;
  float closestDist = 1e20f;
  Collider* closestCollider = nullptr;

  for (Collider* collider : colliders_) {
    if (!collider->IsEnable()) continue;

    // マスクでフィルタリング
    if (!(collider->GetAttribute() & mask)) {
      continue;
    }

    if (collider->GetShapeType() == Collider::ShapeType::Sphere) {
      SphereCollider* sphere = static_cast<SphereCollider*>(collider);
      float dist = 0.0f;
      if (IsCollision(ray, sphere->GetWorldSphere(), &dist)) {
        if (dist < closestDist) {
          closestDist = dist;
          closestCollider = collider;
          hit = true;
        }
      }
    }
  }

  if (hit) {
    if (outCollider) *outCollider = closestCollider;
    if (outDistance) *outDistance = closestDist;
    return true;
  }

  return false;
}
