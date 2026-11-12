#include "BehaviorStraight.h"
#include "Actor/Enemy.h"
#include "Math/MathUtil.h"
#include <algorithm>

void BehaviorStraight::Update(Enemy* enemy) {
    if (!enemy) return;

    float aliveTime = enemy->GetAliveTime();
    float speed = enemy->GetSpeed();
    float flySpeed = speed * 1.25f; // 前進速度を適度に引き上げ、疾走感を出す

    // 指定された移動方向ベクトル（カメラ基準）に従って純粋に直線移動
    Vector3 localDir = enemy->GetMoveDirection();
    if (localDir.x == 0.0f && localDir.y == 0.0f && localDir.z == 0.0f) {
        localDir = { 0.0f, 0.0f, 1.0f };
    }
    float len = std::sqrt(localDir.x * localDir.x + localDir.y * localDir.y + localDir.z * localDir.z);
    if (len > 0.0001f) {
        localDir.x /= len;
        localDir.y /= len;
        localDir.z /= len;
    }

    if (enemy->GetCamera()) {
        Vector3 camFwd = enemy->GetBaseForward();
        Vector3 camRight = enemy->GetBaseRight();
        Vector3 camUp = enemy->GetBaseUp();
        moveDirection_ = {
            camRight.x * localDir.x + camUp.x * localDir.y + camFwd.x * localDir.z,
            camRight.y * localDir.x + camUp.y * localDir.y + camFwd.y * localDir.z,
            camRight.z * localDir.x + camUp.z * localDir.y + camFwd.z * localDir.z
        };
    } else {
        moveDirection_ = localDir;
    }
    isInitialized_ = true;

    // レールの向きと移動ベクトルに合わせてモデルのY軸回転を合わせる
    enemy->GetTransform().rotate.y = std::atan2(moveDirection_.x, moveDirection_.z);

    // 飛行中の姿勢変化を表現する微小ロール角（サイン波）の付加
    float swayRoll = std::sin(aliveTime * 3.0f) * 0.05f;
    enemy->GetTransform().rotate.z = swayRoll;
    enemy->GetTransform().rotate.x = 0.0f;

    // 前方への巡航速度の計算
    // 自機のカメラ速度（約0.92m/frame）に合わせ、追突されない同期巡航速度（0.88m/frame）を基準とする
    float cruiseSpeed = (enemy->GetCamera() && localDir.z > 0.0f) ? 0.88f : flySpeed;
    float currentSpeed = cruiseSpeed;

    // 背後から出現した敵は、最初の1.5秒間で自機を一気に追い抜いて前方50mへ展開するブーストを適用
    if (enemy->GetSpawnOffset().z < 0.0f && localDir.z > 0.0f && aliveTime < 1.5f) {
        float boostRatio = (1.5f - aliveTime) / 1.5f;
        currentSpeed += boostRatio * boostRatio * 2.25f; // 初速ブーストで一気に前方50mへ飛び出す
    }

    // 背後から自機を追い抜いた敵は、撃つ猶予（4.5秒間）を与えた後、奥の建造物に衝突する前に上空へブレイクして画面外へ離脱
    if (enemy->GetSpawnOffset().z < 0.0f && localDir.z > 0.0f && aliveTime >= 4.5f) {
        float escapeRatio = aliveTime - 4.5f;
        moveDirection_.y += escapeRatio * 0.8f;
        currentSpeed += escapeRatio * 0.4f;
        enemy->GetTransform().rotate.x = -std::min(escapeRatio * 0.4f, 0.35f); // 機首を上げて上昇感を出す
    }

    // 指定されたベクトルに沿って移動
    enemy->GetTransform().translate.x += moveDirection_.x * currentSpeed;
    enemy->GetTransform().translate.y += moveDirection_.y * currentSpeed;
    enemy->GetTransform().translate.z += moveDirection_.z * currentSpeed;

    // 出現から 2 秒たったあと、画面の外に 1 秒続けていたら消滅
    if (aliveTime > 2.0f && enemy->HasLeftScreen()) {
        enemy->Destroy();
        return;
    }
    // 保険としての最大寿命判定（10秒）
    else if (aliveTime > 10.0f) {
        enemy->Destroy();
        return;
    }
}
