#pragma once
#include "Math/Transform.h"
#include <string>

enum class ActorTag {
    Untagged,
    Player,
    Enemy,
    LockOnTarget,
    ShotTarget  // ロックオンはできず、通常ショットの狙いの助けだけが効く相手
};

inline std::string ActorTagToString(ActorTag tag) {
    switch(tag) {
        case ActorTag::Player: return "Player";
        case ActorTag::Enemy: return "Enemy";
        case ActorTag::LockOnTarget: return "LockOnTarget";
        case ActorTag::ShotTarget: return "ShotTarget";
        case ActorTag::Untagged:
        default: return "Untagged";
    }
}

inline ActorTag StringToActorTag(const std::string& str) {
    if (str == "Player") return ActorTag::Player;
    if (str == "Enemy") return ActorTag::Enemy;
    if (str == "LockOnTarget") return ActorTag::LockOnTarget;
    if (str == "ShotTarget") return ActorTag::ShotTarget;
    return ActorTag::Untagged;
}

/// <summary>
/// 全てのゲームオブジェクトの親となるベースクラス
/// </summary>
class BaseActor {
public:
    BaseActor();
    virtual ~BaseActor() = default;

    virtual void Initialize() {}
    virtual void Update() {}
    virtual void UpdateTransform() {} // トランスフォーム（描画用）のみの更新処理
    virtual void Draw3D() {} // 3Dモデルなどの描画
    virtual void Draw2D() {} // UIやカーソルなどの2D描画

    // 衝突時のコールバック（Colliderから呼ばれる）
    virtual void OnCollision(class Collider* other) {}

    // 生存フラグの操作
    void Destroy() { isDead_ = true; }
    bool IsDead() const { return isDead_; }

    // Transform情報へのアクセス
    Transform& GetTransform() { return transform_; }
    const Transform& GetTransform() const { return transform_; }

    // 識別用の名前やタグ
    std::string name_ = "Actor";
    ActorTag tag_ = ActorTag::Untagged;

    ActorTag GetTag() const { return tag_; }
    void SetTag(ActorTag tag) { tag_ = tag; }

protected:
    // 3D空間上の位置・回転・スケール
    Transform transform_;
    
    // 生存フラグ（trueになればManagerが自動で削除する）
    bool isDead_ = false;

    // 衝突判定用の前回座標と初期化フラグ
    Vector3 previousPos_ = {0.0f, 0.0f, 0.0f};
    bool hasInitializedPreviousPos_ = false;

    // 連続衝突判定（カプセル）用の速度を計算する
    Vector3 CalculateVelocityForCollision();
};
