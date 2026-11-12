#pragma once
#include "Actor/Enemy.h"
#include "Math/Vector3.h"

class Boss;

// ボスの弱点コアを覆う物理的な装甲板。
// 全て破壊されるまでボス本体へのダメージを防ぐ役割を持つ。
class BossBit : public Enemy {
public:
    BossBit();
    ~BossBit() override;

    void Initialize() override;
    void Update() override;
    void TakeDamage(int damage, bool isSelfDestruct = false) override;
    // 退避中はロックオンできない
    bool CanBeLockedOn() const override { return !isSpreadingOut_; }

    // 親となるボスを設定
    void SetBoss(Boss* boss) { boss_ = boss; }
    // ボス中心からの相対配置オフセットを設定
    void SetOffset(const Vector3& offset);

    // 突進時などの退避アクション
    void SpreadOut();
    // 退避の解除
    void ResetPosition();

    void SetId(int id) { id_ = id; }
    int GetId() const { return id_; }

private:
    int id_ = 0;
    Boss* boss_ = nullptr;
    Vector3 offset_ = {0.0f, 0.0f, 0.0f};
    Vector3 baseOffset_ = {0.0f, 0.0f, 0.0f};
    bool isSpreadingOut_ = false;
};
