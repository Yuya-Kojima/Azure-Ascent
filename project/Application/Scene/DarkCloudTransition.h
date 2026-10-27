#pragma once
#include "Scene/SceneManager.h"

/// <summary>
/// 暗い雲の遷移（ゲームオーバーで拠点へ落ちるとき・暗い空の拠点から出撃するとき）
/// </summary>
namespace DarkCloudTransition {
// 雲の本体（日の当たる面）・影・縁の光の色（sRGB）。見下ろしの雲海に近い暗い灰青
inline const Vector4 kColor = {0.36f, 0.38f, 0.45f, 1.0f};
inline const Vector4 kShadowColor = {0.10f, 0.11f, 0.15f, 1.0f};
inline const Vector4 kGlowColor = {0.55f, 0.58f, 0.66f, 1.0f};

/// <summary>
/// 次の遷移を暗い雲にする（このあと ChangeScene を呼ぶ）
/// </summary>
/// <param name="durationSec">遷移の時間（秒）</param>
/// <param name="type">雲が上から覆うか（出撃）、下から覆うか（落ちる）</param>
inline void SetNext(float durationSec, Fade::FadeType type) {
  SceneManager *sceneManager = SceneManager::GetInstance();
  sceneManager->SetNextTransitionFade(durationSec, type, kColor,
                                      Fade::EasingType::Linear);
  sceneManager->SetNextTransitionCloudShade(kShadowColor, kGlowColor);
}
} // namespace DarkCloudTransition
