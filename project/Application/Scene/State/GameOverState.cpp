#include "Scene/State/GameOverState.h"
#include "Scene/GamePlayScene.h"

void GameOverState::Enter() { GetScene().StartGameOver(); }

float GameOverState::GetWorldTimeScale() const {
  return GetScene().GetGameOverTimeScale();
}

const ICamera *GameOverState::UpdateSequence(bool canMove) {
  return GetScene().UpdateGameOverScreen(canMove);
}

bool GameOverState::MovesWing(bool, bool isSequenceMoving) const {
  // 時間がゆっくりになっても、翼は普通の速さで進める
  return isSequenceMoving;
}

float GameOverState::GetHudAlpha(float) const {
  // 被弾から HUD を消していく
  return GetScene().GetGameOverHudAlpha();
}

void GameOverState::DrawInFrontOfCards() { GetScene().DrawGameOverScreen(); }
