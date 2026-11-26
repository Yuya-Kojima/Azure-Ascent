#pragma once
#include "Scene/State/GamePlayState.h"

/// <summary>
/// ゲームオーバーの状態。入ったときにゲームオーバーの演出を始め、演出が自機を動かす（演出の終わりに拠点へ切り替える）
/// </summary>
class GameOverState : public GamePlayState {
public:
  using GamePlayState::GamePlayState;

  void Enter() override;

  float GetWorldTimeScale() const override;
  const ICamera *UpdateSequence(bool canMove) override;

  bool IsPlayerPosedBySequence() const override { return true; }
  bool MovesWing(bool isWorldUpdated, bool isSequenceMoving) const override;

  float GetHudAlpha(float playAlpha) const override;
  void DrawInFrontOfCards() override;
};
