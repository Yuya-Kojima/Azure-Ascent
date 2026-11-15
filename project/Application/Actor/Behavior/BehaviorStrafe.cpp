#include "BehaviorStrafe.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include "Actor/EnemyBullet.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include <cmath>

BehaviorStrafe::BehaviorStrafe() : stateTimer_(0.0f), shotTimer_(0) {}

void BehaviorStrafe::Update(Enemy* enemy) {
  if (!enemy) return;

  stateTimer_ += 1.0f / 60.0f;
  shotTimer_++;

  auto camera = enemy->GetCamera();
  auto player = enemy->GetPlayer();
  if (!camera || !player) return;

  const Vector3& cameraPos = enemy->GetBasePosition();
  const Vector3& cameraRight = enemy->GetBaseRight();
  const Vector3& cameraUp = enemy->GetBaseUp();
  const Vector3& cameraForward = enemy->GetBaseForward();
  
  float aliveTime = enemy->GetAliveTime();
  float speed = enemy->GetSpeed(); 
  const Vector3& spawnOffset = enemy->GetSpawnOffset();

  // 横断方向の決定 (右からなら左へ、左からなら右へ)
  float direction = (spawnOffset.x > 0.0f) ? -1.0f : 1.0f;
  float currentXOffset = spawnOffset.x + (direction * speed * aliveTime * 60.0f);
  
  enemy->GetTransform().translate =
      cameraPos +
      Vector3{cameraRight.x * currentXOffset, cameraRight.y * currentXOffset, cameraRight.z * currentXOffset} +
      Vector3{cameraUp.x * spawnOffset.y, cameraUp.y * spawnOffset.y, cameraUp.z * spawnOffset.y} +
      Vector3{cameraForward.x * spawnOffset.z, cameraForward.y * spawnOffset.z, cameraForward.z * spawnOffset.z};

  // 正面（手前）を向かせる
  enemy->GetTransform().rotate.y = 3.14159f; 

  // 射撃（手前に向かって弾をばらまく）
  if (shotTimer_ >= 45) { // 0.75秒に1発
    shotTimer_ = 0;
    
    // カメラの前方（手前）へ向かうベクトル
    float bulletSpeed = 1.5f;
    Vector3 bulletVelocity = {
        -cameraForward.x * bulletSpeed,
        -cameraForward.y * bulletSpeed,
        -cameraForward.z * bulletSpeed
    };

    auto bullet = std::make_unique<EnemyBullet>();
    bullet->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer(), enemy->GetTransform().translate, bulletVelocity, player);
    ActorManager::GetInstance()->AddActor(std::move(bullet));
  }
}
