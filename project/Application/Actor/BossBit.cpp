#include "Actor/BossBit.h"
#include "Actor/Boss.h"
#include "Collision/SphereCollider.h"

BossBit::BossBit() {}
BossBit::~BossBit() {}

void BossBit::Initialize() {
    Enemy::Initialize();
    
    // 装甲の HP
    hp_ = 30;
    
    if (collider_) {
        collider_->SetRadius(1.2f); // 装甲板として少し大きめの当たり判定
    }
}

void BossBit::SetOffset(const Vector3& offset) {
    baseOffset_ = offset;
    offset_ = offset;
}

void BossBit::SpreadOut() {
    isSpreadingOut_ = true; // ロックオン不可にする
    baseColor_ = {0.3f, 0.3f, 0.3f, 1.0f}; // 退避中の色（暗い灰色）
}

void BossBit::ResetPosition() {
    isSpreadingOut_ = false; // ロックオン可能に戻す
    baseColor_ = {1.0f, 1.0f, 1.0f, 1.0f}; // 元の色に戻す
}

void BossBit::TakeDamage(int damage, bool isSelfDestruct) {
    if (isSpreadingOut_) {
        return; // 退避中はダメージ無効
    }
    Enemy::TakeDamage(damage, isSelfDestruct);
}

void BossBit::Update() {
    // 親が死んでいるか存在しなければ自壊
    if (!boss_ || boss_->GetHP() <= 0) {
        hp_ = 0;
        isDead_ = true;
    }

    if (!isDead_ && boss_) {
        // 親の現在座標とスケールを取得
        Vector3 bossPos = boss_->GetTransform().translate;
        Vector3 bossScale = boss_->GetTransform().scale;
        Vector3 bossRot = boss_->GetTransform().rotate;
        
        // 退避中であっても物理的な移動は行わず、元の位置を維持する
        Vector3 targetOffset = baseOffset_;
        
        // 滑らかに移動させる
        offset_.x += (targetOffset.x - offset_.x) * 0.1f;
        offset_.y += (targetOffset.y - offset_.y) * 0.1f;
        offset_.z += (targetOffset.z - offset_.z) * 0.1f;

        // Y軸回転を適用（ボスがプレイヤーの方を向く回転に追従する）
        float cosY = std::cos(bossRot.y);
        float sinY = std::sin(bossRot.y);
        
        Vector3 scaledOffset;
        scaledOffset.x = offset_.x * bossScale.x;
        scaledOffset.y = offset_.y * bossScale.y;
        scaledOffset.z = offset_.z * bossScale.z;
        
        Vector3 rotatedOffset;
        rotatedOffset.x = scaledOffset.x * cosY + scaledOffset.z * sinY;
        rotatedOffset.y = scaledOffset.y;
        rotatedOffset.z = -scaledOffset.x * sinY + scaledOffset.z * cosY;

        transform_.translate.x = bossPos.x + rotatedOffset.x;
        transform_.translate.y = bossPos.y + rotatedOffset.y;
        transform_.translate.z = bossPos.z + rotatedOffset.z;
        
        // 向きもボスに合わせる
        transform_.rotate = bossRot;
        
        // 自身のスケールもボスの大きさに合わせる
        transform_.scale.x = bossScale.x * 0.6f;
        transform_.scale.y = bossScale.y * 0.6f;
        transform_.scale.z = bossScale.z * 0.6f;
    }

    Enemy::Update();
}
