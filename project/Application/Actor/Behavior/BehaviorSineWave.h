#pragma once
#include "IEnemyBehavior.h"
#include "Math/Vector3.h"

// 波打ちながら進むAI
class BehaviorSineWave : public IEnemyBehavior {
public:
    void Update(Enemy* enemy) override;

private:
    bool isInitialized_ = false;
    Vector3 moveDirection_ = {0.0f, 0.0f, 0.0f};
    Vector3 rightDirection_ = {0.0f, 0.0f, 0.0f};
    Vector3 initialPos_ = {0.0f, 0.0f, 0.0f};
    Vector3 prevBasePos_ = {0.0f, 0.0f, 0.0f}; // 前のフレームのレールの基準位置
};
