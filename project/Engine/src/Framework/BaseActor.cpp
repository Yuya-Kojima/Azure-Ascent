#include "Framework/BaseActor.h"

BaseActor::BaseActor() {
    // 初期値の設定
    transform_.scale = {1.0f, 1.0f, 1.0f};
    transform_.rotate = {0.0f, 0.0f, 0.0f};
    transform_.translate = {0.0f, 0.0f, 0.0f};
}

void BaseActor::PrepareForPool() {
    // 派生クラスの片付け
    OnReturnToPool();

    // 作ったばかりの状態に戻す
    transform_.scale = {1.0f, 1.0f, 1.0f};
    transform_.rotate = {0.0f, 0.0f, 0.0f};
    transform_.translate = {0.0f, 0.0f, 0.0f};
    isDead_ = false;
    previousPos_ = {0.0f, 0.0f, 0.0f};
    hasInitializedPreviousPos_ = false;
    tag_ = ActorTag::Untagged;

    // 目印を作り直し、前の目印を配った ActorRef を期限切れにする
    lifeToken_ = std::make_shared<char>(0);
}

Vector3 BaseActor::CalculateVelocityForCollision() {
    if (!hasInitializedPreviousPos_) {
        previousPos_ = transform_.translate; // 1フレーム目は移動量ゼロにする
        hasInitializedPreviousPos_ = true;
    }
    Vector3 vel = {transform_.translate.x - previousPos_.x,
                   transform_.translate.y - previousPos_.y,
                   transform_.translate.z - previousPos_.z};
    previousPos_ = transform_.translate;
    return vel;
}
