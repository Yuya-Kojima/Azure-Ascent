#include "RailCamera.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>
#include <numbers>

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

  if (!isFinished_ && isAutoMove_) {
    // 時間で進行度を進める (speed_ は 1秒間に何セグメント進むか)
    t_ += speed_ * speedScale_ * (1.0f / 60.0f);

    float maxT = static_cast<float>(waypoints_.size() - 1);
    if (t_ >= maxT) {
      t_ = maxT;
      isFinished_ = true;
    }
  }

  // 現在の座標を計算
  Vector3 currentPos = CalcPosition(t_);

  transform_.translate = currentPos;
  transform_.translate.y +=
      2.5f; // レールシューター的な俯瞰オフセット（高さを上げる）

  // カメラシェイクの適用
  if (shakeTimer_ > 0.0f) {
    float power = shakeIntensity_ * (shakeTimer_ / shakeDuration_);
    float rx = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;
    float ry = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;
    float rz = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * power;
    transform_.translate.x += rx;
    transform_.translate.y += ry;
    transform_.translate.z += rz;

    shakeTimer_ -= 1.0f / 60.0f;
    if (shakeTimer_ <= 0.0f) {
      shakeTimer_ = 0.0f;
      shakeIntensity_ = 0.0f;
    }
  }

  // --- バンク・首振りの計算（プレイヤー連動） ---
  Vector3 tangentNow = CalcTangent(t_);
  Vector3 worldUp = {0.0f, 1.0f, 0.0f};
  Vector3 right = SafeNormalize(Cross(worldUp, tangentNow));
  Vector3 upApprox = SafeNormalize(Cross(tangentNow, right)); // レールの近似上方向

  // レールの基準ベクトルと座標を保存しておく（敵などがカメラの首振りやシェイクに影響されずに移動するため）
  railPos_ = currentPos;
  railPos_.y += 2.5f;
  railForward_ = tangentNow;
  railRight_ = right;
  railUp_ = upApprox;

  // レール中心からプレイヤーまでのローカルなズレを計算
  Vector3 diffToPlayer = {playerWorldPos_.x - transform_.translate.x,
                          playerWorldPos_.y - transform_.translate.y,
                          playerWorldPos_.z - transform_.translate.z};
  float localX = Dot(diffToPlayer, right);
  float localY = Dot(diffToPlayer, upApprox);

  // プレイヤーのズレ → 目標のバンク角（プレイヤーが右にいれば右に傾く）
  float targetBankAmount = localX * bankStrength_ *
                           -0.02f; // 自機の可動域縮小に合わせ、傾きの係数を強化

  // バンク角を滑らかに補間する（急な傾きを防いで酔いを防止）
  currentBankAmount_ = Lerp(currentBankAmount_, targetBankAmount,
                            0.03f); // カメラ側も少し鈍感にする

  // upを現在のバンク角（currentBankAmount_）ぶんrightに向けて傾ける
  Vector3 up = SafeNormalize({upApprox.x - right.x * currentBankAmount_,
                              upApprox.y - right.y * currentBankAmount_,
                              upApprox.z - right.z * currentBankAmount_});

  // 少し下を向く俯瞰オフセットを適用しつつ、プレイヤー方向に少し首を振る
  float lookDistance = 10.0f; // 注視点までの距離
  float lookWeightX = 0.40f;  // 左右の引っ張り具合（元に戻す）
  float lookWeightY = 0.25f;  // 上下の引っ張り具合（元に戻す）

  Vector3 lookTarget = {transform_.translate.x + tangentNow.x * lookDistance +
                            right.x * (localX * lookWeightX),
                        transform_.translate.y + tangentNow.y * lookDistance +
                            upApprox.y * (localY * lookWeightY) -
                            1.0f, // 少し下を向く
                        transform_.translate.z + tangentNow.z * lookDistance +
                            right.z * (localX * lookWeightX)};

  // フォーカス対象が設定されている場合は注視点をそちらに向ける
  if (focusTarget_ && focusWeight_ > 0.0f) {
      lookTarget.x = Lerp(lookTarget.x, focusTarget_->x, focusWeight_);
      lookTarget.y = Lerp(lookTarget.y, focusTarget_->y, focusWeight_);
      lookTarget.z = Lerp(lookTarget.z, focusTarget_->z, focusWeight_);
  }

  // --- カメラ行列を基底ベクトルから直接構築 ---
  viewMatrix_ = MakeLookAtMatrix(transform_.translate, lookTarget, up);
  projectionMatrix_ =
      MakePerspectiveFovMatrix(fov_, aspectRatio_, nearClip_, farClip_);
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

Vector3 RailCamera::CalcTangent(float t) const {
  float delta = 0.01f;
  float maxT = static_cast<float>(waypoints_.size() - 1);
  Vector3 p0 = CalcPosition((std::max)(t - delta, 0.0f));
  Vector3 p1 = CalcPosition((std::min)(t + delta, maxT));
  Vector3 tangent = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
  return SafeNormalize(tangent);
}
