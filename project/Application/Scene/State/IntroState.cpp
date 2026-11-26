#include "Scene/State/IntroState.h"
#include "Scene/GamePlayScene.h"
#include "Scene/State/PlayState.h"

void IntroState::Enter() { GetScene().StartIntro(); }

std::unique_ptr<GamePlayState> IntroState::Update() {
  // 飛び出しに合わせて操作を始める（演出は続き、操作の効き具合を少しずつ上げる）
  if (GetScene().IsIntroControlStarted()) {
    GetScene().FinishIntro();
    return std::make_unique<PlayState>(GetScene());
  }
  return nullptr;
}

bool IntroState::MovesRailWhileWorldStopped() const {
  // 開始演出の後半は、操作の前からレールを動かし始める
  return GetScene().GetIntroRailSpeedRatio() > 0.0f;
}
