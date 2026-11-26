#pragma once
#include "IEnemyBehavior.h"
#include "Math/Vector3.h"
#include <memory>

/// <summary>
/// 画面外からスムーズに進入し、自機前方でホバリング滞空してプレイヤーと対峙、
/// 一定時間後に回避・離脱行動を取る汎用自律戦闘機AI。
/// 各フェーズの時間や浮遊の振幅・周波数はConfig構造体を通じて外部から柔軟にカスタマイズ可能。
/// 登場・戦闘・回避・離脱の 4 つの状態クラスを持ち、今の状態クラスが次の状態を決める。
/// </summary>
class BehaviorFighter : public IEnemyBehavior {
public:
    /// <summary>
    /// 汎用自律戦闘機AIの動作パラメータ
    /// </summary>
    struct Config {
        float enterDuration = 0.9f;        // 画面外から定位置までの進入時間（秒）
        float combatDuration = 4.2f;       // 自機前方でホバリング滞空する時間（秒）
        float evadeDuration = 1.0f;        // 回避行動の時間（秒）
        float hoverAmplitude = 1.6f;       // ホバリングの上下浮遊の振幅（m）
        float hoverFrequency = 2.0f;       // ホバリングの浮遊周波数（rad/s）
        float rollAmplitude = 0.12f;       // 生き物・飛行感を表すロール角の揺らぎ（rad）
        float enterOffsetDistance = 75.0f; // 画面外の出現オフセット距離（左右方向）
    };

    /// <summary>
    /// 状態の基底クラス（BehaviorFighter.cpp で定義する）
    /// </summary>
    class State;

    BehaviorFighter();
    explicit BehaviorFighter(const Config& config);
    ~BehaviorFighter() override;
    void Update(Enemy* enemy) override;

    void SetConfig(const Config& config) { config_ = config; }
    const Config& GetConfig() const { return config_; }

private:
    std::unique_ptr<State> state_; // 今の状態
    Config config_;
};
