#pragma once
#include "Math/Transform.h"
#include <memory>
#include <string>

class IActorPool;

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

    // 目印を Actor ごとに 1 つにするため、コピーを禁止する
    BaseActor(const BaseActor&) = delete;
    BaseActor& operator=(const BaseActor&) = delete;

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

    // 何者か（敵・自機・狙える物など）を表すタグ
    ActorTag GetTag() const { return tag_; }
    void SetTag(ActorTag tag) { tag_ = tag; }

    // この Actor が破棄されるか、入れ物へしまわれるまで期限切れにならない目印（ActorRef が使う）
    std::weak_ptr<const void> GetLifeToken() const { return lifeToken_; }

    // 消えたときに返す入れ物（ActorPool から取り出した Actor だけが持つ）
    void SetPool(IActorPool* pool) { pool_ = pool; }
    IActorPool* GetPool() const { return pool_; }

    /// <summary>
    /// 入れ物へしまう前の片付け。派生クラスの片付けをさせてから、作ったばかりの状態に戻し、
    /// 目印を新しくする（前の目印を持つ ActorRef は、破棄されたときと同じく nullptr を返すようになる）
    /// </summary>
    void PrepareForPool();

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

    /// <summary>
    /// 入れ物へしまう直前の、派生クラスの片付け（当たり判定を外す・消えたことを知らせるなど）
    /// </summary>
    virtual void OnReturnToPool() {}

private:
    ActorTag tag_ = ActorTag::Untagged;

    // Actor と一緒に破棄され（入れ物へしまうときは作り直され）、GetLifeToken() で配った weak_ptr を期限切れにする
    std::shared_ptr<const void> lifeToken_ = std::make_shared<char>(0);

    IActorPool* pool_ = nullptr; // 消えたときに返す入れ物
};
