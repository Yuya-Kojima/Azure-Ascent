#include "Scene/State/ClearState.h"
#include "Scene/GamePlayScene.h"

void ClearState::Enter() {
  if (bossLastHit_) {
    GetScene().StartClearSequence(*bossLastHit_);
  } else {
    GetScene().StartClear();
  }
}

std::unique_ptr<GamePlayState> ClearState::Update() {
  GetScene().UpdateClear();
  return nullptr;
}

float ClearState::GetWorldTimeScale() const {
  return GetScene().GetClearTimeScale();
}

const ICamera *ClearState::UpdateSequence(bool canMove) {
  // ボス・自機を演出に合わせて動かす（リザルト中も最後の構図で映す）
  return GetScene().UpdateClearScreen(canMove);
}

float ClearState::GetHudAlpha(float) const {
  return GetScene().GetClearHudAlpha();
}

bool ClearState::ShowsHud() const {
  // クリア演出の間は、最後の一撃から消えていく HUD を出す
  return !GetScene().IsResultActive();
}

void ClearState::DrawBehindCards() { GetScene().DrawResult(); }
