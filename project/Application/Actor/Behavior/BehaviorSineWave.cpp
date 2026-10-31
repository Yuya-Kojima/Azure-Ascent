#include "BehaviorSineWave.h"
#include "Actor/Enemy.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>

void BehaviorSineWave::Update(Enemy* enemy) {
    if (!enemy) return;

    float aliveTime = enemy->GetAliveTime();
    float speed = enemy->GetSpeed();
    float flySpeed = speed * 1.15f;

    if (!isInitialized_) {
        isInitialized_ = true;
        moveDirection_ = enemy->GetBaseForward();
        rightDirection_ = enemy->GetBaseRight();
        prevBasePos_ = enemy->GetBasePosition();
        if (!enemy->GetCamera()) {
            Vector3 moveDir = enemy->GetMoveDirection();
            moveDirection_ = { moveDir.x, moveDir.y, moveDir.z };
            rightDirection_ = { 1.0f, 0.0f, 0.0f };
        }
    }

    // 前進量（カメラがあるときはレールがこのフレームに進んだ分だけ進み、カメラからの奥行きを保つ）
    Vector3 forwardStep = { moveDirection_.x * flySpeed, moveDirection_.y * flySpeed, moveDirection_.z * flySpeed };

    // カメラの最新基準方向を取得（レールのカーブに自然に追従）
    if (enemy->GetCamera()) {
        moveDirection_ = enemy->GetBaseForward();
        rightDirection_ = enemy->GetBaseRight();
        const Vector3& basePos = enemy->GetBasePosition();
        forwardStep = { basePos.x - prevBasePos_.x, basePos.y - prevBasePos_.y, basePos.z - prevBasePos_.z };
        prevBasePos_ = basePos;
    }

    // 出現位置のXオフセットに基づいて横断方向を決定（左から出たら右へ、右から出たら左へ横断）
    float crossDir = (enemy->GetSpawnOffset().x < 0.0f) ? 1.0f : -1.0f;

    // 敵ごとの配置に基づくわずかな時間差（先頭機から順に0.36秒ずつ追従遅延）
    float rankIndex = (std::max)(0.0f, (std::abs(enemy->GetSpawnOffset().x) - 130.0f) / 10.0f);
    float phaseDelay = rankIndex * 0.36f;
    float t = (std::max)(0.0f, aliveTime - phaseDelay);

    // 画面外から出現し、画面中央を横断して反対側へ抜ける水平滑空移動
    float lateralSpeed = crossDir * 0.85f;

    // コサイン波による上下方向の周期運動（振幅 0.18m/frame）。追従遅延の間は上下に動かず、出現した高さを中心にうねる
    Vector3 upDir = enemy->GetBaseUp();
    float waveSpeed = (aliveTime < phaseDelay) ? 0.0f : std::cos(t * 1.4f) * 0.18f;

    // 前進 ＋ 横断 ＋ 上下うねりの合成移動
    enemy->GetTransform().translate.x += forwardStep.x + rightDirection_.x * lateralSpeed + upDir.x * waveSpeed;
    enemy->GetTransform().translate.y += forwardStep.y + rightDirection_.y * lateralSpeed + upDir.y * waveSpeed;
    enemy->GetTransform().translate.z += forwardStep.z + rightDirection_.z * lateralSpeed + upDir.z * waveSpeed;

    // 進行方向ベクトルに合わせて機首（Y回転）をスムーズに向ける
    Vector3 totalMoveDir = {
        forwardStep.x + rightDirection_.x * lateralSpeed,
        forwardStep.y + rightDirection_.y * lateralSpeed + upDir.y * waveSpeed,
        forwardStep.z + rightDirection_.z * lateralSpeed
    };
    enemy->GetTransform().rotate.y = std::atan2(totalMoveDir.x, totalMoveDir.z);

    // 横断方向および波の位相に応じたロール角（Z軸回転）の適用
    float targetRoll = -crossDir * 0.35f + std::sin(t * 1.4f) * 0.15f;
    enemy->GetTransform().rotate.z = targetRoll;
    enemy->GetTransform().rotate.x = -waveSpeed * 0.5f; // 上下移動に連動したピッチ角

    // 出現から 12 秒で消滅（カメラの後ろへ回った敵は Enemy::Update で消える）
    if (aliveTime > 12.0f) {
        enemy->Destroy();
    }
}
