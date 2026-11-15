#include "Camera/BossCameraDirector.h"
#include "Camera/RailCamera.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kFrameTime = 1.0f / 60.0f; // 1 フレームの秒数
} // namespace

void BossCameraDirector::OnEvent(Boss::CameraEvent event, RailCamera &railCamera) {
  switch (event) {
  case Boss::CameraEvent::CounterSucceeded:
    railCamera.Shake(kCounterShakeIntensity_, kCounterShakeSeconds_);
    break;
  case Boss::CameraEvent::BarrageFired:
    railCamera.Shake(kBarrageShakeIntensity_, kBarrageShakeSeconds_);
    break;
  case Boss::CameraEvent::MissilesDeployed:
    railCamera.Shake(kMissileShakeIntensity_, kMissileShakeSeconds_);
    break;
  case Boss::CameraEvent::DashStarted:
    railCamera.Shake(kDashShakeIntensity_, kDashShakeSeconds_);
    break;
  case Boss::CameraEvent::TransitionStarted:
    flow_ = Flow::ZoomIn;
    flowTimer_ = 0.0f;
    break;
  case Boss::CameraEvent::TransitionEnded:
    // 画角と注視は寄ったまま、戻す流れへ移る
    railCamera.Shake(kTransitionEndShakeIntensity_, kTransitionEndShakeSeconds_);
    flow_ = Flow::PullBack;
    flowTimer_ = 0.0f;
    break;
  }
}

void BossCameraDirector::Update(const Boss *boss, RailCamera &railCamera) {
  if (flow_ == Flow::None) {
    return;
  }
  if (!boss) {
    Finish(railCamera);
    return;
  }
  flowTimer_ += kFrameTime;
  const Vector3 &bossPos = boss->GetTransform().translate;

  if (flow_ == Flow::ZoomIn) {
    // 最初の kZoomInSeconds_ 秒で画角を狭め、同じ緩急で注視を強めて、そのまま保つ
    const float zoomT = (std::min)(flowTimer_ / kZoomInSeconds_, 1.0f);
    const float easeZoom = zoomT * zoomT * (3.0f - 2.0f * zoomT);
    railCamera.SetFov(std::lerp(RailCamera::kDefaultFov_, kZoomFov_, easeZoom));
    railCamera.SetFocus(bossPos, easeZoom);
    // 形態変化が進むほど揺れを強める
    railCamera.Shake(kZoomInShakeMaxIntensity_ * boss->GetTransitionProgress(), kZoomInShakeSeconds_);
    return;
  }

  // 寄ったまま kHoldSeconds_ 秒保ち、その後 kPullBackSeconds_ 秒で画角と注視を戻す（イーズアウト）
  float pullBackT = 0.0f;
  if (flowTimer_ > kHoldSeconds_) {
    pullBackT = (std::min)((flowTimer_ - kHoldSeconds_) / kPullBackSeconds_, 1.0f);
  }
  const float easePullBack = 1.0f - std::pow(1.0f - pullBackT, 3.0f);
  railCamera.SetFov(std::lerp(kZoomFov_, RailCamera::kDefaultFov_, easePullBack));
  railCamera.SetFocus(bossPos, 1.0f - easePullBack);
  if (flowTimer_ >= kHoldSeconds_ + kPullBackSeconds_) {
    Finish(railCamera);
  }
}

void BossCameraDirector::Reset() {
  flow_ = Flow::None;
  flowTimer_ = 0.0f;
}

void BossCameraDirector::Finish(RailCamera &railCamera) {
  railCamera.SetFov(RailCamera::kDefaultFov_);
  railCamera.ClearFocus();
  Reset();
}
