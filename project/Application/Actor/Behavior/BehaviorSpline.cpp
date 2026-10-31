#include "BehaviorSpline.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Actor/EnemyBullet.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include <cmath>
#include <algorithm>

BehaviorSpline::BehaviorSpline(const std::vector<Vector3>& waypoints, float duration, bool isWorldSpace, int fireInterval)
    : waypoints_(waypoints), duration_(duration), isWorldSpace_(isWorldSpace), fireInterval_(fireInterval), fireTimer_(0) {
}

void BehaviorSpline::Update(Enemy* enemy) {
    if (!enemy || waypoints_.size() < 4) return;

    float aliveTime = enemy->GetAliveTime();

    int maxSegments = static_cast<int>(waypoints_.size()) - 1;
    if (maxSegments < 1) return;

    if (duration_ <= 0.0f) duration_ = 1.0f; // ゼロ割防止
    float totalT = (aliveTime / duration_) * static_cast<float>(maxSegments);

    
    // 走り終わったら、爆発させずに消す（撃破に数えないので、点数では逃した敵になる）
    if (totalT >= maxSegments) {
        enemy->Destroy();
        return;
    }

    int segmentIndex = static_cast<int>(totalT);
    if (segmentIndex >= maxSegments) {
        segmentIndex = maxSegments - 1;
    }

    float t = totalT - segmentIndex;

    auto getWaypoint = [&](int index) -> const Vector3& {
        return waypoints_[(std::max)(0, (std::min)(index, static_cast<int>(waypoints_.size() - 1)))];
    };

    const Vector3& p0 = getWaypoint(segmentIndex - 1);
    const Vector3& p1 = getWaypoint(segmentIndex);
    const Vector3& p2 = getWaypoint(segmentIndex + 1);
    const Vector3& p3 = getWaypoint(segmentIndex + 2);

    // Spline座標の計算 (MathUtil.hのCatmullRomを使用)
    Vector3 localPos = CatmullRom(p0, p1, p2, p3, t);

    // 向きの計算 (少し先の未来の座標を見てそこを向く)
    float tNext = t + 0.05f;
    Vector3 nextLocalPos;
    if (tNext >= 1.0f && segmentIndex + 1 < maxSegments) {
        const Vector3& np0 = getWaypoint(segmentIndex);
        const Vector3& np1 = getWaypoint(segmentIndex + 1);
        const Vector3& np2 = getWaypoint(segmentIndex + 2);
        const Vector3& np3 = getWaypoint(segmentIndex + 3);
        nextLocalPos = CatmullRom(np0, np1, np2, np3, tNext - 1.0f);
    } else {
        tNext = std::min(1.0f, tNext);
        nextLocalPos = CatmullRom(p0, p1, p2, p3, tNext);
    }
    
    Vector3 nextWorldPos;

    if (isWorldSpace_) {
        enemy->GetTransform().translate = localPos;
        nextWorldPos = nextLocalPos;
    } else {
        // カメラ相対座標系へ変換
        const Vector3& cameraPos = enemy->GetBasePosition();
        const Vector3& cameraRight = enemy->GetBaseRight();
        const Vector3& cameraUp = enemy->GetBaseUp();
        const Vector3& cameraForward = enemy->GetBaseForward();
        const Vector3& spawnOffset = enemy->GetSpawnOffset();

        // スポーンオフセットも加味する（軌道全体をオフセットぶんずらす）
        Vector3 finalLocal = {
            localPos.x + spawnOffset.x,
            localPos.y + spawnOffset.y,
            localPos.z + spawnOffset.z
        };

        enemy->GetTransform().translate = 
            cameraPos +
            Vector3{cameraRight.x * finalLocal.x, cameraRight.y * finalLocal.x, cameraRight.z * finalLocal.x} +
            Vector3{cameraUp.x * finalLocal.y, cameraUp.y * finalLocal.y, cameraUp.z * finalLocal.y} +
            Vector3{cameraForward.x * finalLocal.z, cameraForward.y * finalLocal.z, cameraForward.z * finalLocal.z};
        
        Vector3 nextFinalLocal = {
            nextLocalPos.x + spawnOffset.x,
            nextLocalPos.y + spawnOffset.y,
            nextLocalPos.z + spawnOffset.z
        };

        nextWorldPos = 
            cameraPos +
            Vector3{cameraRight.x * nextFinalLocal.x, cameraRight.y * nextFinalLocal.x, cameraRight.z * nextFinalLocal.x} +
            Vector3{cameraUp.x * nextFinalLocal.y, cameraUp.y * nextFinalLocal.y, cameraUp.z * nextFinalLocal.y} +
            Vector3{cameraForward.x * nextFinalLocal.z, cameraForward.y * nextFinalLocal.z, cameraForward.z * nextFinalLocal.z};
    }

    Vector3 dir = {
        nextWorldPos.x - enemy->GetTransform().translate.x,
        nextWorldPos.y - enemy->GetTransform().translate.y,
        nextWorldPos.z - enemy->GetTransform().translate.z
    };
    
    if (std::abs(dir.x) > 0.001f || std::abs(dir.z) > 0.001f) {
        enemy->GetTransform().rotate.y = std::atan2(dir.x, dir.z);
    }
    
    // 射撃処理
    if (fireInterval_ >= 0) {
        fireTimer_++;
        if (fireTimer_ >= fireInterval_) {
            fireTimer_ = 0;
            const Player* player = enemy->GetPlayer();
            if (player) {
                Vector3 pPos = player->GetTransform().translate;
                Vector3 mPos = enemy->GetTransform().translate;
                Vector3 bulletDir = {pPos.x - mPos.x, pPos.y - mPos.y, pPos.z - mPos.z};
                float dist = std::sqrt(bulletDir.x*bulletDir.x + bulletDir.y*bulletDir.y + bulletDir.z*bulletDir.z);
                if (dist > 0.001f) {
                    bulletDir.x /= dist; bulletDir.y /= dist; bulletDir.z /= dist;
                    float bulletSpeed = 2.0f; // 弾速
                    Vector3 bulletVel = {bulletDir.x * bulletSpeed, bulletDir.y * bulletSpeed, bulletDir.z * bulletSpeed};
                    auto bullet = std::make_unique<EnemyBullet>();
                    bullet->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer(),
                                       mPos, bulletVel, const_cast<Player*>(player), EnemyBulletType::NormalDestructible);
                    ActorManager::GetInstance()->AddActor(std::move(bullet));
                }
            }
        }
    }
}
