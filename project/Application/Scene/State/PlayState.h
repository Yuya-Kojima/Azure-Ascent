#pragma once
#include "Scene/State/GamePlayState.h"

/// <summary>
/// 操作中の状態。自機が倒れたらゲームオーバー、ボスを倒すかボスなしでレールが終わったらクリアへ移る
/// </summary>
class PlayState : public GamePlayState {
public:
  using GamePlayState::GamePlayState;

  std::unique_ptr<GamePlayState> Update() override;
  std::unique_ptr<GamePlayState> OnBossLastHit(const BossLastHit &hit) override;

  bool CanPause() const override { return true; }
  bool CanEndByDebug() const override { return true; }
  bool ShowsAimUI() const override { return true; }
};
