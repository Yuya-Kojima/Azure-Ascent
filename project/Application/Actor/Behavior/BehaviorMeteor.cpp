#include "BehaviorMeteor.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include <cmath>

BehaviorMeteor::BehaviorMeteor() : stateTimer_(0.0f) {}

void BehaviorMeteor::Update(Enemy* enemy) {
  if (!enemy) return;

  stateTimer_ += 1.0f / 60.0f;

  auto camera = enemy->GetCamera();
  auto player = enemy->GetPlayer();
  if (!camera || !player) return;

  Vector3 currentCameraPos = camera->GetTranslate();
  if (isFirstFrame_) {
    previousCameraPos_ = currentCameraPos;
    isFirstFrame_ = false;
  }
  Vector3 cameraDelta = {
      currentCameraPos.x - previousCameraPos_.x,
      currentCameraPos.y - previousCameraPos_.y,
      currentCameraPos.z - previousCameraPos_.z
  };
  previousCameraPos_ = currentCameraPos;

  if (state_ == State::Wait) {
    // 待機中はカメラ相対位置を維持
    const Vector3& cameraPos = enemy->GetBasePosition();
    const Vector3& cameraRight = enemy->GetBaseRight();
    const Vector3& cameraUp = enemy->GetBaseUp();
    const Vector3& cameraForward = enemy->GetBaseForward();
    const Vector3& spawnOffset = enemy->GetSpawnOffset();

    enemy->GetTransform().translate =
        cameraPos +
        Vector3{cameraRight.x * spawnOffset.x, cameraRight.y * spawnOffset.x, cameraRight.z * spawnOffset.x} +
        Vector3{cameraUp.x * spawnOffset.y, cameraUp.y * spawnOffset.y, cameraUp.z * spawnOffset.y} +
        Vector3{cameraForward.x * spawnOffset.z, cameraForward.y * spawnOffset.z, cameraForward.z * spawnOffset.z};

    // プレイヤーの方を向く
    Vector3 playerPos = player->GetTransform().translate;
    Vector3 myPos = enemy->GetTransform().translate;
    Vector3 dirToPlayer = {
        playerPos.x - myPos.x,
        playerPos.y - myPos.y,
        playerPos.z - myPos.z
    };
    enemy->GetTransform().rotate.y = std::atan2(dirToPlayer.x, dirToPlayer.z);

    // 1秒後に突撃開始
    if (stateTimer_ >= 1.0f) {
      state_ = State::Charge;
      stateTimer_ = 0.0f;
      
      // 突撃速度の計算
      float dist = std::sqrt(dirToPlayer.x * dirToPlayer.x + dirToPlayer.y * dirToPlayer.y + dirToPlayer.z * dirToPlayer.z);
      if (dist > 0.001f) {
        float chargeSpeed = enemy->GetSpeed() * 5.0f; // かなり速い
        chargeVelocity_ = {
            (dirToPlayer.x / dist) * chargeSpeed,
            (dirToPlayer.y / dist) * chargeSpeed,
            (dirToPlayer.z / dist) * chargeSpeed
        };
      }
    }
  } else if (state_ == State::Charge) {
    // 突撃状態（ワールド座標で等速直線運動 ＋ カメラの移動量を加算）
    // カメラの移動量を足すことで、自機が動かなければ必ず命中するようになる
    enemy->GetTransform().translate.x += chargeVelocity_.x + cameraDelta.x;
    enemy->GetTransform().translate.y += chargeVelocity_.y + cameraDelta.y;
    enemy->GetTransform().translate.z += chargeVelocity_.z + cameraDelta.z;
  }
}
