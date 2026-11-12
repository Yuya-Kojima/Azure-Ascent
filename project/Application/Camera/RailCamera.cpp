#include "RailCamera.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// v を単位ベクトル axis のまわりに angle（ラジアン）回す（ロドリゲスの回転公式）。
// 正の角度で、axis の前にある向きは Cross(axis, 前) の向きへ回る
Vector3 RotateAroundAxis(const Vector3 &v, const Vector3 &axis, float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  return v * c + Cross(axis, v) * s + axis * (Dot(axis, v) * (1.0f - c));
}

constexpr float kFrameTime = 1.0f / 60.0f; // 1 フレームの秒数

// 0〜1 の x を、両端の傾きが 0 になるようにならす
float SmoothStep01(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

} // namespace

RailCamera::RailCamera() {
  transform_.scale = {1.0f, 1.0f, 1.0f};
  transform_.rotate = {0.0f, 0.0f, 0.0f};
  transform_.translate = {0.0f, 0.0f, 0.0f};

  fov_ = 45.0f * (std::numbers::pi_v<float> / 180.0f);
  aspectRatio_ = 1280.0f / 720.0f;
  nearClip_ = 0.1f;
  farClip_ = 1000.0f;

  worldMatrix_ = MakeIdentity4x4();
  viewMatrix_ = MakeIdentity4x4();
  projectionMatrix_ = MakeIdentity4x4();
  viewProjectionMatrix_ = MakeIdentity4x4();
}

void RailCamera::Initialize(const std::vector<Vector3> &waypoints) {
  waypoints_ = waypoints;
  t_ = 0.0f;
  isFinished_ = false;

  if (waypoints_.size() > 0) {
    transform_.translate = waypoints_[0];
  }
}

void RailCamera::Update() {
  if (waypoints_.size() < 2)
    return;

  // 出来事の速さの倍率・注視・顔の向きの戻りの時間を進める
  if (eventSpeedTimer_ < eventSpeedSeconds_) {
    eventSpeedTimer_ = (std::min)(eventSpeedTimer_ + kFrameTime, eventSpeedSeconds_);
  }
  if (zoomPunchTimer_ >= 0.0f) {
    zoomPunchTimer_ += kFrameTime;
    if (zoomPunchTimer_ >= zoomPunchIn_ + zoomPunchHold_ + zoomPunchOut_) {
      zoomPunchTimer_ = -1.0f;
    }
  }
  if (!isLookHeld_ && lookReleaseTimer_ < lookReleaseSeconds_) {
    lookReleaseTimer_ = (std::min)(lookReleaseTimer_ + kFrameTime, lookReleaseSeconds_);
  }

  if (!isFinished_ && isAutoMove_) {
    // 時間で進行度を進める (speed_ は 1秒間に何セグメント進むか)
    t_ += speed_ * speedScale_ * GetEventSpeedScale() * kFrameTime;

    float maxT = static_cast<float>(waypoints_.size() - 1);
    if (t_ >= maxT) {
      t_ = maxT;
      isFinished_ = true;
    }
  }

  // --- 1. レール: レールの位置と向き（敵の動きの基準。構図・首振り・揺れの影響を受けない） ---
  const Vector3 tangentNow = CalcTangent(t_);
  const Vector3 worldUp = {0.0f, 1.0f, 0.0f};
  const Vector3 right = SafeNormalize(Cross(worldUp, tangentNow));
  const Vector3 upApprox = SafeNormalize(Cross(tangentNow, right)); // レールの近似上方向
  railPos_ = CalcPosition(t_);
  railPos_.y += kRailHeight_;
  railForward_ = tangentNow;
  railRight_ = right;
  railUp_ = upApprox;

  // --- 2. 構図: レールから少し見下ろし、注視対象と顔の向きのキーで位置と向きを決める ---
  // 自機はこの構図を土台に置かれるので、ここでは自機の位置を使わない
  Vector3 framePos = railPos_;
  Vector3 frameTarget = framePos + tangentNow * kLookDistance_ + Vector3{0.0f, -kLookDown_, 0.0f};
  Vector3 frameUp = upApprox;

  // フォーカス対象が設定されている場合は注視点をそちらに向ける
  if (focusTarget_ && focusWeight_ > 0.0f) {
    frameTarget.x = Lerp(frameTarget.x, focusTarget_->x, focusWeight_);
    frameTarget.y = Lerp(frameTarget.y, focusTarget_->y, focusWeight_);
    frameTarget.z = Lerp(frameTarget.z, focusTarget_->z, focusWeight_);
  }

  // 顔の向きのキー（位置をレールの右へずらし、見る向きを振って傾ける）
  const LookKey look = CurrentLook();
  if (look.yaw != 0.0f || look.roll != 0.0f || look.out != 0.0f) {
    // 位置をレールの右へずらす（注視点も同じだけずらし、見る向きは変えない）
    const Vector3 shift = right * look.out;
    framePos += shift;
    frameTarget += shift;

    // 見る向きと上の向きを、レールの上の軸まわりに yaw だけ回す（正で右へ）
    const float yawRad = look.yaw * (std::numbers::pi_v<float> / 180.0f);
    const Vector3 viewDir = RotateAroundAxis(frameTarget - framePos, upApprox, yawRad);
    frameUp = SafeNormalize(RotateAroundAxis(frameUp, upApprox, yawRad));
    frameTarget = framePos + viewDir;

    // 上の向きを、見る向きの軸まわりに roll だけ傾ける（バンクと同じ向きの正負）
    const float rollRad = look.roll * (std::numbers::pi_v<float> / 180.0f);
    const Vector3 viewForward = SafeNormalize(viewDir);
    const Vector3 viewRight = SafeNormalize(Cross(frameUp, viewForward));
    frameUp = SafeNormalize(frameUp * std::cos(rollRad) - viewRight * std::sin(rollRad));
  }

  // 構図の向きを、互いに直角な前・右・上にそろえる
  framing_.position = framePos;
  framing_.forward = SafeNormalize(frameTarget - framePos);
  framing_.right = SafeNormalize(Cross(frameUp, framing_.forward));
  framing_.up = Cross(framing_.forward, framing_.right);

  // --- 3. 反応: 構図の上での自機のずれに合わせて首を振って傾け、揺れを足す ---
  // 自機は構図を土台に置かれているので、構図から測ったずれは、自機が照準で動いた量そのものになる
  const Vector3 toPlayer = playerWorldPos_ - framing_.position;
  const float localX = Dot(toPlayer, framing_.right);
  const float localY = Dot(toPlayer, framing_.up);

  // プレイヤーのズレ → 目標のバンク角（プレイヤーが右にいれば右に傾く）
  const float targetBankAmount = localX * bankStrength_ * kBankScale_;

  // バンク角を、1 フレームに kBankFollow_ の割合だけ目標へ近づける
  currentBankAmount_ = Lerp(currentBankAmount_, targetBankAmount, kBankFollow_);

  // カメラシェイク（位置を揺らし、注視点も同じだけ動かす）
  Vector3 shake = {0.0f, 0.0f, 0.0f};
  if (shakeTimer_ > 0.0f) {
    const float power = shakeIntensity_ * (shakeTimer_ / shakeDuration_);
    shake.x = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;
    shake.y = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;
    shake.z = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;

    shakeTimer_ -= kFrameTime;
    if (shakeTimer_ <= 0.0f) {
      shakeTimer_ = 0.0f;
      shakeIntensity_ = 0.0f;
    }
  }
  transform_.translate = framing_.position + shake;

  // 自機の方へ首を振る
  const Vector3 lookTarget = transform_.translate + framing_.forward * kLookDistance_ +
                             framing_.right * (localX * kFollowWeightX_) +
                             framing_.up * (localY * kFollowWeightY_);

  // up を今のバンク角（currentBankAmount_）ぶん右へ傾ける
  const Vector3 up = SafeNormalize(framing_.up - framing_.right * currentBankAmount_);

  // --- カメラ行列を基底ベクトルから直接構築 ---
  viewMatrix_ = MakeLookAtMatrix(transform_.translate, lookTarget, up);
  projectionMatrix_ = MakePerspectiveFovMatrix(fov_, aspectRatio_, nearClip_, farClip_);
  ApplyZoomPunch();
  viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);

  // worldMatrix_ も整合性のために更新（デバッグ用途等に使用）
  worldMatrix_ = Inverse(viewMatrix_);
}

Vector3 RailCamera::CalcPosition(float t) const {
  if (waypoints_.empty())
    return {0.0f, 0.0f, 0.0f};
  if (waypoints_.size() == 1)
    return waypoints_[0];

  int maxIdx = static_cast<int>(waypoints_.size() - 1);
  int i = static_cast<int>(t);
  if (i >= maxIdx) {
    return waypoints_.back();
  }

  float localT = t - static_cast<float>(i);

  // 4つの制御点を取得（範囲外にならないようにClampする）
  int idx0 = std::max(i - 1, 0);
  int idx1 = i;
  int idx2 = std::min(i + 1, maxIdx);
  int idx3 = std::min(i + 2, maxIdx);

  const Vector3 &p0 = waypoints_[idx0];
  const Vector3 &p1 = waypoints_[idx1];
  const Vector3 &p2 = waypoints_[idx2];
  const Vector3 &p3 = waypoints_[idx3];

  return CatmullRom(p0, p1, p2, p3, localT);
}

void RailCamera::Shake(float intensity, float duration) {
  shakeIntensity_ =
      (std::max)(shakeIntensity_, intensity + (shakeIntensity_ * 0.2f));
  shakeDuration_ = (std::max)(shakeDuration_, duration);
  shakeTimer_ = (std::max)(shakeTimer_, duration);
}

void RailCamera::SetLookKeys(const std::vector<LookKey> &keys) {
  lookKeys_ = keys;
  SortLookKeys();
}

void RailCamera::SortLookKeys() {
  std::stable_sort(lookKeys_.begin(), lookKeys_.end(),
                   [](const LookKey &a, const LookKey &b) { return a.t < b.t; });
}

RailCamera::LookKey RailCamera::SampleLook(float t) const {
  LookKey result;
  result.t = t;
  if (lookKeys_.empty() || t < lookKeys_.front().t || t > lookKeys_.back().t) {
    return result;
  }
  // t をはさむ 2 つのキーを探し、その間を smoothstep でつなぐ
  for (size_t i = 0; i + 1 < lookKeys_.size(); ++i) {
    const LookKey &a = lookKeys_[i];
    const LookKey &b = lookKeys_[i + 1];
    if (t > b.t) {
      continue;
    }
    const float span = b.t - a.t;
    const float k = (span > 1e-6f) ? SmoothStep01((t - a.t) / span) : 1.0f;
    result.yaw = Lerp(a.yaw, b.yaw, k);
    result.roll = Lerp(a.roll, b.roll, k);
    result.out = Lerp(a.out, b.out, k);
    return result;
  }
  // キーが 1 つだけで、t がちょうどその位置のとき
  const LookKey &only = lookKeys_.back();
  result.yaw = only.yaw;
  result.roll = only.roll;
  result.out = only.out;
  return result;
}

Vector3 RailCamera::CalcTangent(float t) const {
  float delta = 0.01f;
  float maxT = static_cast<float>(waypoints_.size() - 1);
  Vector3 p0 = CalcPosition((std::max)(t - delta, 0.0f));
  Vector3 p1 = CalcPosition((std::min)(t + delta, maxT));
  Vector3 tangent = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  return SafeNormalize(tangent);
}

void RailCamera::ChangeEventSpeedScale(float scale, float seconds) {
  eventSpeedFrom_ = GetEventSpeedScale();
  eventSpeedTo_ = scale;
  eventSpeedTimer_ = 0.0f;
  eventSpeedSeconds_ = (std::max)(seconds, 0.0f);
}

float RailCamera::GetEventSpeedScale() const {
  if (eventSpeedSeconds_ <= 0.0f) {
    return eventSpeedTo_;
  }
  return Lerp(eventSpeedFrom_, eventSpeedTo_, SmoothStep01(eventSpeedTimer_ / eventSpeedSeconds_));
}

void RailCamera::PunchZoom(const Vector3 &anchor, float fovScale, float inSeconds,
                           float holdSeconds, float outSeconds) {
  zoomPunchAnchor_ = anchor;
  zoomPunchFovScale_ = std::clamp(fovScale, kMinZoomPunchFovScale_, 1.0f);
  zoomPunchIn_ = (std::max)(inSeconds, 0.0f);
  zoomPunchHold_ = (std::max)(holdSeconds, 0.0f);
  zoomPunchOut_ = (std::max)(outSeconds, 0.0f);
  zoomPunchTimer_ = 0.0f;
}

void RailCamera::ApplyZoomPunch() {
  // 画角を fovScale 倍に狭めたときと同じだけ、画面の上の点を拡大する倍率
  const float halfFov = fov_ * 0.5f;
  const float magnification = std::tan(halfFov) / std::tan(halfFov * ZoomPunchFovScale());
  if (magnification <= 1.0f + 1e-4f) {
    return;
  }
  // 拡大の中心: 拡大する前の画面での、見る所の位置（-1〜1）
  const Matrix4x4 &v = viewMatrix_;
  const Matrix4x4 &p = projectionMatrix_;
  const Vector3 &a = zoomPunchAnchor_;
  const Vector3 view = {a.x * v.m[0][0] + a.y * v.m[1][0] + a.z * v.m[2][0] + v.m[3][0],
                        a.x * v.m[0][1] + a.y * v.m[1][1] + a.z * v.m[2][1] + v.m[3][1],
                        a.x * v.m[0][2] + a.y * v.m[1][2] + a.z * v.m[2][2] + v.m[3][2]};
  const float clipX = view.x * p.m[0][0] + view.y * p.m[1][0] + view.z * p.m[2][0] + p.m[3][0];
  const float clipY = view.x * p.m[0][1] + view.y * p.m[1][1] + view.z * p.m[2][1] + p.m[3][1];
  const float clipW = view.x * p.m[0][3] + view.y * p.m[1][3] + view.z * p.m[2][3] + p.m[3][3];
  Vector2 center = {0.0f, 0.0f};
  if (clipW > 1e-3f) {
    center = {std::clamp(clipX / clipW, -1.0f, 1.0f), std::clamp(clipY / clipW, -1.0f, 1.0f)};
  }
  // 画面の上で center を動かさずに拡大する: x' = m × x + (1 − m) × center × w（y も同じ）
  Matrix4x4 zoom = MakeIdentity4x4();
  zoom.m[0][0] = magnification;
  zoom.m[1][1] = magnification;
  zoom.m[3][0] = (1.0f - magnification) * center.x;
  zoom.m[3][1] = (1.0f - magnification) * center.y;
  projectionMatrix_ = Multiply(projectionMatrix_, zoom);
}

float RailCamera::ZoomPunchFovScale() const {
  if (zoomPunchTimer_ < 0.0f) {
    return 1.0f;
  }
  // 押し込む間は 1 → 倍率、保つ間は倍率のまま、戻る間は 倍率 → 1（押し込む・戻るは smoothstep）
  float k = 1.0f;
  if (zoomPunchTimer_ < zoomPunchIn_) {
    k = SmoothStep01(zoomPunchTimer_ / zoomPunchIn_);
  } else if (zoomPunchTimer_ > zoomPunchIn_ + zoomPunchHold_) {
    k = zoomPunchOut_ > 0.0f
            ? 1.0f - SmoothStep01((zoomPunchTimer_ - zoomPunchIn_ - zoomPunchHold_) / zoomPunchOut_)
            : 0.0f;
  }
  return Lerp(1.0f, zoomPunchFovScale_, k);
}

void RailCamera::HoldLook(float holdT) {
  isLookHeld_ = true;
  lookHoldT_ = holdT;
  lookReleaseSeconds_ = 0.0f;
  lookReleaseTimer_ = 0.0f;
}

void RailCamera::ReleaseLook(float seconds) {
  if (!isLookHeld_) {
    return;
  }
  lookReleaseFrom_ = CurrentLook();
  isLookHeld_ = false;
  lookReleaseTimer_ = 0.0f;
  lookReleaseSeconds_ = (std::max)(seconds, 0.0f);
}

RailCamera::LookKey RailCamera::CurrentLook() const {
  if (isLookHeld_) {
    return SampleLook((std::min)(t_, lookHoldT_));
  }
  LookKey now = SampleLook(t_);
  if (lookReleaseTimer_ < lookReleaseSeconds_) {
    // 解いてからの間は、解いたときの値から今の t の値へなめらかに戻す
    const float k = SmoothStep01(lookReleaseTimer_ / lookReleaseSeconds_);
    now.yaw = Lerp(lookReleaseFrom_.yaw, now.yaw, k);
    now.roll = Lerp(lookReleaseFrom_.roll, now.roll, k);
    now.out = Lerp(lookReleaseFrom_.out, now.out, k);
  }
  return now;
}

void RailCamera::ResetEventControls() {
  eventSpeedFrom_ = 1.0f;
  eventSpeedTo_ = 1.0f;
  eventSpeedTimer_ = 0.0f;
  eventSpeedSeconds_ = 0.0f;
  zoomPunchTimer_ = -1.0f;
  isLookHeld_ = false;
  lookReleaseTimer_ = 0.0f;
  lookReleaseSeconds_ = 0.0f;
}
