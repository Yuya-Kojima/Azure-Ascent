#pragma once
#include "Scene/State/GamePlayState.h"

/// <summary>
/// 開始演出の状態。世界を止めて演出が自機を動かし、自機の操作が始まったら操作中の状態へ移る
/// </summary>
class IntroState : public GamePlayState {
public:
  using GamePlayState::GamePlayState;

  void Enter() override;
  std::unique_ptr<GamePlayState> Update() override;

  bool UpdatesWorld() const override { return false; }
  bool MovesRailWhileWorldStopped() const override;
  bool HoldsRailCameraAim() const override { return true; }

  bool IsPlayerPosedBySequence() const override { return true; }
  bool MovesWing(bool, bool) const override { return true; }

  bool ShowsUI() const override { return false; }
  bool ShowsHud() const override { return false; }
};
