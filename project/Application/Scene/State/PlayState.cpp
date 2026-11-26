#include "Scene/State/PlayState.h"
#include "Scene/GamePlayScene.h"
#include "Scene/State/ClearState.h"
#include "Scene/State/GameOverState.h"

std::unique_ptr<GamePlayState> PlayState::Update() {
  // 開始演出のあと、HUD と照準を薄い状態から現す
  GetScene().UpdateHudFadeIn();

  if (GetScene().IsPlayerDead()) {
    return std::make_unique<GameOverState>(GetScene());
  }
  // ボスが生きている場合はレールが終わってもクリアにしない
  if (GetScene().IsRailFinished() && !GetScene().IsBossAlive()) {
    return std::make_unique<ClearState>(GetScene());
  }
  return nullptr;
}

std::unique_ptr<GamePlayState> PlayState::OnBossLastHit(const BossLastHit &hit) {
  return std::make_unique<ClearState>(GetScene(), hit);
}
