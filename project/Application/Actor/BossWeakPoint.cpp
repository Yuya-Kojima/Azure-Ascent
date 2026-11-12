#include "Actor/BossWeakPoint.h"
#include "Actor/Boss.h"
#include "Collision/SphereCollider.h"
#include <cmath>

BossWeakPoint::BossWeakPoint() {}
BossWeakPoint::~BossWeakPoint() {}

void BossWeakPoint::Initialize() {
    Enemy::Initialize();
    
    hp_ = 1; // 1発で壊れるように
    baseColor_ = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤く発光する的
    transform_.scale = {3.0f, 3.0f, 3.0f};
    
    if (collider_) {
        collider_->SetRadius(1.5f); // 狙いやすいように少し大きめ
    }
}

void BossWeakPoint::SetOffset(const Vector3& offset) {
    baseOffset_ = offset;
    offset_ = offset;
}

void BossWeakPoint::Update() {
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
        
        bobbingTimer_ += 0.1f;
        // ふわふわ浮かせる
        offset_.x = baseOffset_.x;
        offset_.y = baseOffset_.y + std::sin(bobbingTimer_) * 0.5f;
        offset_.z = baseOffset_.z;

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
        

    }

    Enemy::Update();
}
