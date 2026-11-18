#include "ClearFlight.h"
#include "Core/WindowSystem.h"
#include "Render/Renderer/LineRenderer.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 乱数の種（同じ種なら同じ飛び方になる）
const unsigned int kRandomSeed = 20261116u;
// 重さによる加速（単位/秒²。釣り合った旋回の傾きと曲がる速さを結ぶ）
const float kGravity = 9.8f;
// 横の回転に入る速さとして扱う、最も遅い速さ（0 で割らない）
const float kMinTurnSpeed = 1.0f;
// 床の少し上から、下を向く舵を止める高さ
const float kFloorMargin = 5.0f;
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};

float WrapAngle(float angle) {
  return std::remainder(angle, 2.0f * std::numbers::pi_v<float>);
}

float Uniform(std::mt19937 &random, float min, float max) {
  return std::uniform_real_distribution<float>(min, max)(random);
}
} // namespace

void ClearFlight::Reset(const Vector3 &cameraPos, const Vector3 &right,
                        const Vector3 &forward, const Vector3 &baseLook,
                        const Vector3 &sunDir, float fovY) {
  cameraPos_ = cameraPos;
  // 止まった画面の上下・左右の半分の角度（画面の横と縦の比から）
  const float toDegrees = 180.0f / std::numbers::pi_v<float>;
  const float aspect = float(WindowSystem::kClientWidth) /
                       float(WindowSystem::kClientHeight);
  const float tanHalfPitch = std::tan(fovY * 0.5f);
  frameHalfPitchDeg_ = std::atan(tanHalfPitch) * toDegrees;
  frameHalfYawDeg_ = std::atan(tanHalfPitch * aspect) * toDegrees;
  right_ = right;
  forward_ = forward;
  baseLook_ = Normalize(baseLook);
  sunDir_ = Normalize(sunDir);

  time_ = 0.0f;
  random_.seed(kRandomSeed);
  sunPassTime_ = -1.0f;
  lastBand_ = Band::Near;
  wanderSide_ = 1.0f;
  hasResultLook_ = false;
  spreadTimer_ = 0.0f;
  rollTimer_ = -1.0f;
  isRollPending_ = false;
  resultStats_ = {};
  resultLeg_ = 0;
  trail_.clear();
  trailTimer_ = 0.0f;
  brake_ = 0.0f;
  bank_ = 0.0f;
  flap_ = 1.0f;
  speed_ = startSpeed_;

  // カメラの右下の後ろから、最初にかすめる点へ向いて飛び出す
  position_ = FromCamera(startOffset_);
  waypoint_ = Waypoint::NearPass;
  target_ = FromCamera(nearPassOffset_);
  targetTimer_ = 0.0f;
  const Vector3 toTarget = target_ - position_;
  yaw_ = std::atan2(toTarget.x, toTarget.z);
  pitch_ = std::atan2(toTarget.y,
                      std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z));

  lookDir_ = baseLook_;
  StepWing();
}

Vector3 ClearFlight::FromCamera(const Vector3 &offset) const {
  return cameraPos_ + right_ * offset.x + kWorldUp * offset.y +
         forward_ * offset.z;
}

Vector3 ClearFlight::Direction() const {
  return {std::sin(yaw_) * std::cos(pitch_), std::sin(pitch_),
          std::cos(yaw_) * std::cos(pitch_)};
}

float ClearFlight::TurnRadius() const {
  // 釣り合った旋回：半径 = 速さ² / (重さの加速 × tan(傾き))
  const float tanBank = std::tan((std::max)(maxBank_, 0.01f));
  return speed_ * speed_ / (kGravity * tanBank);
}

Vector3 ClearFlight::GetRotation() const {
  // 横の回転：rollSec_ かけて一回転（始めと終わりをゆるめる）
  float roll = 0.0f;
  if (rollTimer_ >= 0.0f && rollSec_ > 0.0f) {
    const float t = std::clamp(rollTimer_ / rollSec_, 0.0f, 1.0f);
    roll = rollSide_ * 2.0f * std::numbers::pi_v<float> * EaseInOutCubic(t);
  }
  // モデルの x 回転は下向きが正
  // 減速の間は、進む向きより体を brakePitch_ だけ起こす
  return {-(pitch_ + brakePitch_ * brake_), yaw_, bankSign_ * (bank_ + roll)};
}

Matrix4x4 ClearFlight::MakeOrientationMatrix() const {
  // 角度の回転は x → y → z の順に掛かり z が世界の奥の軸まわりになるので、z → x → y の順で組む
  const Vector3 rotate = GetRotation();
  return Multiply(MakeRotateZMatrix(rotate.z),
                  Multiply(MakeRotateXMatrix(rotate.x), MakeRotateYMatrix(rotate.y)));
}

Quaternion ClearFlight::GetOrientation() const {
  return MakeRotationFromMatrix(MakeOrientationMatrix());
}

Vector3 ClearFlight::GetModelPosition() const {
  // 回していないときの中心（position_ + bodyPivot_）が、回しても動かないように原点をずらす
  return position_ + bodyPivot_ - TransformNormal(bodyPivot_, MakeOrientationMatrix());
}

void ClearFlight::Step(float dt) {
  time_ += dt;
  targetTimer_ += dt;

  //===========================
  // 点に着いた（近づいた・通り過ぎた・時間切れ）ら、次の点へ
  //===========================
  const Vector3 toTarget = target_ - position_;
  const float distance = Length(toTarget);
  // 通り過ぎた：近くで、点が前から passAngleDeg_ より横・後ろにある（自由に飛ぶ間。決まった経路は真横より後ろ）
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  const float passDot = waypoint_ == Waypoint::Wander
                            ? std::cos(passAngleDeg_ * toRadians)
                            : 0.0f;
  const bool passed = distance < reachRadius_ * 3.0f &&
                      Dot(toTarget, Direction()) < passDot * distance;
  const bool arrived =
      distance < reachRadius_ || passed || targetTimer_ > targetTimeout_;
  if (arrived) {
    NextWaypoint();
  }

  StepSteering(dt);
  StepSpeed(dt);
  position_ += Direction() * (speed_ * dt);
  StepPlay(dt);
  StepWing();
  StepCamera(dt);
  if (hasResultLook_) {
    StepResultStats(dt);
  }
  trailTimer_ += dt;
  if (trailTimer_ >= kTrailInterval) {
    trailTimer_ -= kTrailInterval;
    trail_.push_back(position_);
    if (trail_.size() > kTrailCount) {
      trail_.erase(trail_.begin());
    }
  }
}

void ClearFlight::GetTopView(Vector3 &translate, Vector3 &rotate) const {
  const float kHeight = 90.0f;      // カメラの真上の高さ（m）
  const float kAhead = 15.0f;       // 止まる向きの前へずらす距離（m）
  const float kLookDown = 1.55f;    // 見下ろす角度（ラジアン。真下の少し手前）
  const Vector3 look = hasResultLook_ ? resultLook_ : baseLook_;
  const float yaw = std::atan2(look.x, look.z);
  const Vector3 horizontal = {std::sin(yaw), 0.0f, std::cos(yaw)};
  translate = cameraPos_ + horizontal * kAhead + kWorldUp * kHeight;
  rotate = {kLookDown, yaw, 0.0f};
}

void ClearFlight::DrawDebugLines(LineRenderer &lines) const {
  const Vector4 kTrailColor = {1.0f, 0.9f, 0.1f, 1.0f};
  const Vector4 kTargetColor = {1.0f, 0.1f, 0.1f, 1.0f};
  const Vector4 kFrameColor = {1.0f, 1.0f, 1.0f, 1.0f};
  const Vector4 kBoardColor = {1.0f, 0.5f, 0.0f, 1.0f};
  const float kTargetSize = 1.5f;  // 行き先の印の腕の長さ（m）
  const float kFrameDepth = 35.0f; // 画面の枠を描く奥行き（m）

  // 通った跡と、今の位置から行き先へ
  for (size_t i = 1; i < trail_.size(); ++i) {
    lines.DrawLine(trail_[i - 1], trail_[i], kTrailColor);
  }
  if (!trail_.empty()) {
    lines.DrawLine(trail_.back(), position_, kTrailColor);
  }
  lines.DrawLine(position_, target_, {1.0f, 0.4f, 0.4f, 1.0f});
  lines.DrawLine(target_ - Vector3{kTargetSize, 0.0f, 0.0f},
                 target_ + Vector3{kTargetSize, 0.0f, 0.0f}, kTargetColor);
  lines.DrawLine(target_ - Vector3{0.0f, kTargetSize, 0.0f},
                 target_ + Vector3{0.0f, kTargetSize, 0.0f}, kTargetColor);
  lines.DrawLine(target_ - Vector3{0.0f, 0.0f, kTargetSize},
                 target_ + Vector3{0.0f, 0.0f, kTargetSize}, kTargetColor);

  // リザルトの間に止まる画面の枠（カメラから kFrameDepth 奥）と、板の左端
  if (!hasResultLook_) {
    return;
  }
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  const Vector3 right = Normalize(Cross(kWorldUp, resultLook_));
  const Vector3 up = Cross(resultLook_, right);
  const float halfWidth = kFrameDepth * std::tan(frameHalfYawDeg_ * toRadians);
  const float halfHeight = kFrameDepth * std::tan(frameHalfPitchDeg_ * toRadians);
  const Vector3 center = cameraPos_ + resultLook_ * kFrameDepth;
  auto corner = [&](float x, float y) {
    return center + right * (halfWidth * x) + up * (halfHeight * y);
  };
  const Vector3 corners[4] = {corner(-1.0f, -1.0f), corner(1.0f, -1.0f),
                              corner(1.0f, 1.0f), corner(-1.0f, 1.0f)};
  for (int i = 0; i < 4; ++i) {
    lines.DrawLine(corners[i], corners[(i + 1) % 4], kFrameColor);
    lines.DrawLine(cameraPos_, corners[i], kFrameColor);
  }
  lines.DrawLine(corner(boardLeftNdc_, -1.0f), corner(boardLeftNdc_, 1.0f),
                 kBoardColor);
}

void ClearFlight::StepResultStats(float dt) {
  ResultStats &stats = resultStats_;
  stats.total += dt;
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  const Vector3 toPosition = position_ - cameraPos_;
  const float depth = Dot(toPosition, resultLook_);
  // 画面の横の位置（-1: 左端 〜 1: 右端）。板の左端より右なら板の裏
  bool isBehindBoard = false;
  const bool isInFrame = IsInResultFrame(position_);
  if (isInFrame) {
    const Vector3 right = Normalize(Cross(kWorldUp, resultLook_));
    const float screenX = Dot(toPosition, right) / depth /
                          std::tan(frameHalfYawDeg_ * toRadians);
    isBehindBoard = screenX > boardLeftNdc_;
  }
  stats.legTime[resultLeg_] += dt;
  if (isBehindBoard) {
    stats.boardSec += dt;
    stats.legBoard[resultLeg_] += dt;
  }
  if (!isInFrame) {
    stats.legOut[resultLeg_] += dt;
    // どちらの外か：カメラの後ろ、または画面の端からはみ出した割合が大きい方
    int where = 0;
    if (depth > 0.0f) {
      const Vector3 right = Normalize(Cross(kWorldUp, resultLook_));
      const Vector3 up = Cross(resultLook_, right);
      const float screenX = Dot(toPosition, right) / depth /
                            std::tan(frameHalfYawDeg_ * toRadians);
      const float screenY = Dot(toPosition, up) / depth /
                            std::tan(frameHalfPitchDeg_ * toRadians);
      if (std::abs(screenX) >= std::abs(screenY)) {
        where = screenX < 0.0f ? 1 : 2;
      } else {
        where = screenY > 0.0f ? 3 : 4;
      }
    }
    stats.outWhere[where] += dt;
  }
  if (!isInFrame || isBehindBoard) {
    stats.absent += dt;
    stats.maxAbsent = (std::max)(stats.maxAbsent, stats.absent);
    return;
  }
  stats.absent = 0.0f;
  // 映る大きさ：ドラゴンの大きさ ÷ その奥行きで画面の高さに入る長さ
  const float screenHeight =
      2.0f * depth * std::tan(frameHalfPitchDeg_ * toRadians);
  const float ratio = measureSpan_ / (std::max)(screenHeight, 0.001f);
  if (ratio >= largeRatio_) {
    stats.largeSec += dt;
  } else if (ratio >= smallRatio_) {
    stats.mediumSec += dt;
  } else {
    stats.smallSec += dt;
  }
}

void ClearFlight::NextWaypoint() {
  targetTimer_ = 0.0f;
  switch (waypoint_) {
  case Waypoint::NearPass:
    // 太陽の前へ駆け上がる
    waypoint_ = Waypoint::Sun;
    target_ = cameraPos_ + sunDir_ * sunDistance_;
    return;
  case Waypoint::Sun:
    // 太陽の前で翼を広げて浮き、そのあと左下へ降りる
    sunPassTime_ = time_;
    spreadTimer_ = spreadSec_;
    waypoint_ = Waypoint::DiveLow;
    target_ = FromCamera(diveLowOffset_);
    return;
  case Waypoint::DiveLow:
    waypoint_ = Waypoint::NearPassAgain;
    target_ = FromCamera(nearPassAgainOffset_);
    return;
  case Waypoint::NearPassAgain:
    waypoint_ = Waypoint::OverHead;
    target_ = FromCamera(overHeadOffset_);
    return;
  case Waypoint::OverHead:
  case Waypoint::Wander:
    waypoint_ = Waypoint::Wander;
    target_ = PickWanderTarget();
    // ときどき横に一回転する（始めるのは、画面の中をまっすぐ速く飛んでいるとき。StepPlay で見る）
    isRollPending_ = Uniform(random_, 0.0f, 1.0f) <
                     (hasResultLook_ ? resultRollChance_ : rollChance_);
    return;
  }
}

Vector3 ClearFlight::PickWanderTarget() {
  if (hasResultLook_) {
    return PickResultTarget();
  }
  // 帯を選び、左右を入れ替えて、その中の点を選ぶ（今の位置に近すぎる点は選び直す）
  lastBand_ = PickBand();
  wanderSide_ = -wanderSide_;
  const int kMaxTries = 16;
  Vector3 target = position_;
  for (int i = 0; i < kMaxTries; ++i) {
    target = PickPointInBand(lastBand_, wanderSide_);
    // 曲がる半径より近い点は、回り込まないと届かないので選ばない
    const float minHop = (std::max)(minHopDistance_, TurnRadius() * 2.0f);
    if (Length(target - position_) >= minHop) {
      return target;
    }
  }
  return target;
}

ClearFlight::Band ClearFlight::PickBand() {
  // 遠くの次は遠くにしない（戻ってくる）。近くの次は近くにしない（見せ場を続けない）
  const float mid = midWeight_;
  const float nearWeight = lastBand_ == Band::Near ? 0.0f : nearWeight_;
  const float farWeight = lastBand_ == Band::Far ? 0.0f : farWeight_;
  const float total = mid + nearWeight + farWeight;
  if (total <= 0.0f) {
    return Band::Mid;
  }
  const float pick = Uniform(random_, 0.0f, total);
  if (pick < nearWeight) {
    return Band::Near;
  }
  if (pick < nearWeight + farWeight) {
    return Band::Far;
  }
  return Band::Mid;
}

Vector3 ClearFlight::PickPointInBand(Band band, float side) {
  if (band == Band::Near) {
    // カメラの脇（side の側）を抜ける点
    return FromCamera({side * Uniform(random_, nearSide_.x, nearSide_.y),
                       Uniform(random_, nearUp_.x, nearUp_.y),
                       Uniform(random_, nearForward_.x, nearForward_.y)});
  }
  // 中・遠く：カメラの元の向きから side の側へ 0〜spreadYawDeg_ 度ずらし、見上げる角度を選ぶ
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  const float baseYaw = std::atan2(baseLook_.x, baseLook_.z);
  const float yaw =
      baseYaw + side * Uniform(random_, 0.0f, spreadYawDeg_) * toRadians;
  const float elevation =
      Uniform(random_, minElevationDeg_, maxElevationDeg_) * toRadians;
  const bool isFar = band == Band::Far;
  const float distance =
      isFar ? Uniform(random_, farMinDistance_, farMaxDistance_)
            : Uniform(random_, midMinDistance_, midMaxDistance_);
  const Vector3 direction = {std::sin(yaw) * std::cos(elevation),
                             std::sin(elevation),
                             std::cos(yaw) * std::cos(elevation)};
  return cameraPos_ + direction * distance;
}

bool ClearFlight::IsInResultFrame(const Vector3 &position) const {
  return IsInFrame(position, resultLook_);
}

bool ClearFlight::IsInFrame(const Vector3 &position, const Vector3 &look) const {
  // look を奥、水平な右を右とした画面の座標で、端の角度の内側か
  const Vector3 forward = look;
  const Vector3 right = Normalize(Cross(kWorldUp, forward));
  const Vector3 up = Cross(forward, right);
  const Vector3 toPosition = position - cameraPos_;
  const float depth = Dot(toPosition, forward);
  if (depth <= 0.0f) {
    return false;
  }
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  return std::abs(Dot(toPosition, right)) / depth <
             std::tan(frameHalfYawDeg_ * toRadians) &&
         std::abs(Dot(toPosition, up)) / depth <
             std::tan(frameHalfPitchDeg_ * toRadians);
}

Vector3 ClearFlight::PickResultTarget() {
  //===========================
  // 候補を resultCandidates_ 個作り、選ばれやすさの重みで 1 つ選ぶ（今の位置に近すぎる候補は選ばない）
  //===========================
  const int count = (std::max)(static_cast<int>(resultCandidates_), 1);
  // 曲がる半径より近い点は、回り込まないと届かないので選ばない
  const float minHop = (std::max)(minHopDistance_, TurnRadius() * 2.0f);
  std::vector<Vector3> candidates(count);
  std::vector<ResultLeg> legs(count);
  std::vector<float> weights(count);
  float total = 0.0f;
  for (int i = 0; i < count; ++i) {
    candidates[i] = MakeResultCandidate(legs[i]);
    weights[i] = Length(candidates[i] - position_) < minHop
                     ? 0.0f
                     : ResultCandidateWeight(candidates[i]);
    total += weights[i];
  }
  int chosen = 0;
  if (total > 0.0f) {
    float pick = Uniform(random_, 0.0f, total);
    for (int i = 0; i < count; ++i) {
      chosen = i;
      pick -= weights[i];
      if (pick < 0.0f) {
        break;
      }
    }
  }
  resultLeg_ = static_cast<int>(legs[chosen]);
  return candidates[chosen];
}

Vector3 ClearFlight::MakeResultCandidate(ResultLeg &leg) {
  // resultPassChance_ の割合で、カメラのすぐ脇を抜ける点
  if (Uniform(random_, 0.0f, 1.0f) < resultPassChance_) {
    leg = ResultLeg::Pass;
    const float side = Uniform(random_, 0.0f, 1.0f) < 0.5f ? -1.0f : 1.0f;
    return FromCamera({side * Uniform(random_, nearSide_.x, nearSide_.y),
                       Uniform(random_, nearUp_.x, nearUp_.y),
                       Uniform(random_, nearForward_.x, nearForward_.y)});
  }
  // 止まった画面を左右・上へ広げた範囲のどこか（高さは resultMaxHeight_ まで）
  const float toDegrees = 180.0f / std::numbers::pi_v<float>;
  const float lookPitchDeg =
      std::asin(std::clamp(resultLook_.y, -1.0f, 1.0f)) * toDegrees;
  const float yawRange = frameHalfYawDeg_ + resultYawMarginDeg_;
  const float maxElevation =
      lookPitchDeg + frameHalfPitchDeg_ + resultPitchMarginDeg_;
  Vector3 candidate = FromResultView(
      Uniform(random_, -yawRange, yawRange),
      Uniform(random_, resultMinElevationDeg_, maxElevation),
      Uniform(random_, resultDistance_.x, resultDistance_.y));
  candidate.y = (std::min)(candidate.y, cameraPos_.y + resultMaxHeight_);
  leg = IsInResultFrame(candidate) ? ResultLeg::In : ResultLeg::Out;
  return candidate;
}

float ClearFlight::ResultCandidateWeight(const Vector3 &candidate) const {
  //===========================
  // 最近通った場所（通った跡の直近 recentSec_ 秒）に近いほど選ばれにくい
  //===========================
  const size_t kStride = 5; // 通った跡を何個おきに見るか
  const size_t recentCount = (std::min)(
      trail_.size(), static_cast<size_t>((std::max)(recentSec_, 0.0f) / kTrailInterval));
  float nearest = recentRadius_;
  for (size_t i = trail_.size() - recentCount; i < trail_.size(); i += kStride) {
    nearest = (std::min)(nearest, Length(trail_[i] - candidate));
  }
  const float away = std::clamp(nearest / (std::max)(recentRadius_, 0.001f), 0.0f, 1.0f);
  float weight = Lerp(recentMinWeight_, 1.0f, away * away);

  //===========================
  // 画面の左ほど選ばれやすく、板の裏に映る点は選ばれにくい
  //===========================
  Vector2 screen{};
  if (ToResultScreen(candidate, screen)) {
    const float right = std::clamp((screen.x + 1.0f) * 0.5f, 0.0f, 1.0f);
    weight *= Lerp(1.0f, rightWeight_, right);
    if (IsInResultFrame(candidate) && screen.x > boardLeftNdc_) {
      weight *= boardWeight_;
    }
  }
  return weight;
}

Vector3 ClearFlight::FromResultView(float yawDeg, float elevationDeg,
                                    float distance) const {
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  const float yaw = std::atan2(resultLook_.x, resultLook_.z) + yawDeg * toRadians;
  const Vector3 horizontal = {std::sin(yaw), 0.0f, std::cos(yaw)};
  return cameraPos_ + horizontal * distance +
         kWorldUp * (distance * std::tan(elevationDeg * toRadians));
}

bool ClearFlight::ToResultScreen(const Vector3 &position, Vector2 &screen) const {
  const Vector3 right = Normalize(Cross(kWorldUp, resultLook_));
  const Vector3 up = Cross(resultLook_, right);
  const Vector3 toPosition = position - cameraPos_;
  const float depth = Dot(toPosition, resultLook_);
  if (depth <= 0.0f) {
    return false;
  }
  const float toRadians = std::numbers::pi_v<float> / 180.0f;
  screen = {Dot(toPosition, right) / depth / std::tan(frameHalfYawDeg_ * toRadians),
            Dot(toPosition, up) / depth / std::tan(frameHalfPitchDeg_ * toRadians)};
  return true;
}

void ClearFlight::StepSteering(float dt) {
  const Vector3 toTarget = target_ - position_;
  const float horizontal =
      std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
  float targetPitch = std::atan2(toTarget.y, horizontal);
  float yawGain = 1.0f;
  if (spreadTimer_ > 0.0f) {
    // 翼を広げて浮く間は、水平へ戻しながらゆるく曲がる
    targetPitch = 0.0f;
    yawGain = 0.3f;
  }
  // 床に近いときは下を向かない
  const float floorY = cameraPos_.y + floorOffset_;
  if (position_.y < floorY + kFloorMargin) {
    targetPitch = (std::max)(targetPitch, 0.1f);
  }

  //===========================
  // 横：曲がりたい速さから体の傾きを決め、傾いた分だけ曲がる（釣り合った旋回）
  // 傾きは bankRate_ でしか変わらないので、舵を切るとまず体が傾き、少し遅れて向きが変わる
  //===========================
  const float yawError = WrapAngle(std::atan2(toTarget.x, toTarget.z) - yaw_);
  const float wantYawRate = std::clamp(yawError * turnGain_ * yawGain,
                                       -maxYawRate_, maxYawRate_);
  const float turnSpeed = (std::max)(speed_, kMinTurnSpeed);
  const float wantBank = std::clamp(
      std::atan(wantYawRate * turnSpeed / kGravity), -maxBank_, maxBank_);
  bank_ += std::clamp(wantBank - bank_, -bankRate_ * dt, bankRate_ * dt);
  yaw_ = WrapAngle(yaw_ + kGravity * std::tan(bank_) / turnSpeed * dt);

  //===========================
  // 上下：向きのずれに比例した速さで向きを変える
  //===========================
  const float pitchRate = std::clamp((targetPitch - pitch_) * pitchGain_,
                                     -maxPitchRate_, maxPitchRate_);
  const float maxPitch = hasResultLook_ ? resultMaxPitch_ : maxPitch_;
  pitch_ = std::clamp(pitch_ + pitchRate * dt, -maxPitch, maxPitch);
}

float ClearFlight::CruiseSpeed() const {
  return hasResultLook_ ? resultCruiseSpeed_ : cruiseSpeed_;
}

void ClearFlight::StepSpeed(float dt) {
  const Vector3 direction = Direction();
  const float up = direction.y;
  //===========================
  // 減速：行き先との向きのずれが大きいほど、翼を広げて体を起こし空気で減速する
  //===========================
  float wantBrake = 0.0f;
  if (waypoint_ == Waypoint::Wander && spreadTimer_ <= 0.0f) {
    const float toDegrees = 180.0f / std::numbers::pi_v<float>;
    const Vector3 toTarget = Normalize(target_ - position_);
    const float angleDeg =
        std::acos(std::clamp(Dot(direction, toTarget), -1.0f, 1.0f)) * toDegrees;
    wantBrake = std::clamp((angleDeg - brakeStartDeg_) /
                               (std::max)(brakeFullDeg_ - brakeStartDeg_, 0.001f),
                           0.0f, 1.0f);
  }
  brake_ += (wantBrake - brake_) * std::clamp(dt / brakeLag_, 0.0f, 1.0f);

  //===========================
  // 羽ばたき：遅いほど、上っているほど強く羽ばたく（翼を広げて浮く間・減速の間は羽ばたかない）
  //===========================
  float wantFlap = std::clamp((CruiseSpeed() - speed_) / flapSpeedRange_ +
                                  (std::max)(up, 0.0f) * climbFlap_,
                              0.0f, 1.0f) *
                   (1.0f - brake_);
  if (spreadTimer_ > 0.0f) {
    wantFlap = 0.0f;
  }
  flap_ += (wantFlap - flap_) * std::clamp(dt / flapLag_, 0.0f, 1.0f);

  //===========================
  // 速さ：上りで減り下りで増え、羽ばたくと増え、滑る速さとの差の分だけ減る。減速の間は brakeSpeed_ まで落とす
  //===========================
  float accel = -slopeAccel_ * up + flapAccel_ * flap_ -
                drag_ * (speed_ - glideSpeed_);
  if (speed_ > brakeSpeed_) {
    accel -= brakeDecel_ * brake_;
  }
  speed_ = std::clamp(speed_ + accel * dt, minSpeed_, maxSpeed_);
}

void ClearFlight::StepPlay(float dt) {
  if (spreadTimer_ > 0.0f) {
    spreadTimer_ = (std::max)(spreadTimer_ - dt, 0.0f);
  }
  // 横の回転：画面の中を、旋回も上り下りもせず速く飛んでいるときに始める
  if (isRollPending_ && rollTimer_ < 0.0f &&
      speed_ >= (std::min)(rollMinSpeed_, CruiseSpeed()) &&
      std::abs(bank_) <= rollMaxBank_ && std::abs(pitch_) <= rollMaxPitch_ &&
      IsInFrame(position_, lookDir_)) {
    isRollPending_ = false;
    rollTimer_ = 0.0f;
    rollSide_ = bank_ >= 0.0f ? 1.0f : -1.0f;
  }
  if (rollTimer_ >= 0.0f) {
    rollTimer_ += dt;
    if (rollTimer_ >= rollSec_) {
      rollTimer_ = -1.0f;
    }
  }
}

void ClearFlight::StepWing() {
  // 羽ばたきの強さの割合で、滑る翼と強く羽ばたく翼の間を決める（翼を広げて浮く間は打たない）
  wing_ = {};
  // 減速の間は、翼を広げたまま体を起こした姿勢へ寄せる
  wing_.pose = 1.0f;
  wing_.hover = brakeHover_ * brake_;
  // リザルトの間は、見た目の羽ばたきを resultMinFlap_ より弱くしない
  const float flap = hasResultLook_ ? (std::max)(flap_, resultMinFlap_) : flap_;
  wing_.strength = spreadTimer_ > 0.0f
                       ? 0.0f
                       : Lerp(glideStrength_, flapStrength_, flap) * (1.0f - brake_);
  wing_.frequency = Lerp(glideFrequency_, flapFrequency_, flap);
}

void ClearFlight::StepCamera(float dt) {
  //===========================
  // リザルトの間：最初の向きから決めた角度だけずらした向きへ、settleSec_ かけて戻って止まる
  //===========================
  if (IsResultShown()) {
    if (!hasResultLook_) {
      const float toRadians = std::numbers::pi_v<float> / 180.0f;
      const float yaw = std::atan2(baseLook_.x, baseLook_.z) +
                        resultYawOffsetDeg_ * toRadians;
      const float pitch = std::asin(std::clamp(baseLook_.y, -1.0f, 1.0f)) +
                          resultPitchOffsetDeg_ * toRadians;
      resultLook_ = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                     std::cos(yaw) * std::cos(pitch)};
      resultStartLook_ = lookDir_;
      hasResultLook_ = true;
    }
    const float settle = EaseInOutCubic(std::clamp(
        (time_ - resultTime_) / (std::max)(settleSec_, 0.001f), 0.0f, 1.0f));
    lookDir_ = Normalize(Lerp(resultStartLook_, resultLook_, settle));
    return;
  }

  //===========================
  // 向きたい向き：元の向きとドラゴンの向きの間（ドラゴンが後ろへ行ったら元の向き）
  //===========================
  const Vector3 toDragon = Normalize(position_ - cameraPos_);
  Vector3 want = baseLook_;
  if (Dot(toDragon, baseLook_) > behindDot_) {
    want = Normalize(Lerp(baseLook_, toDragon, followWeight_));
  }
  // 下を向きすぎない
  const float minY = std::sin(minLookPitch_);
  if (want.y < minY) {
    const float horizontal = std::sqrt(want.x * want.x + want.z * want.z);
    const float scale = std::sqrt(1.0f - minY * minY) / (std::max)(horizontal, 0.0001f);
    want = {want.x * scale, minY, want.z * scale};
  }

  //===========================
  // 遅れて向き、向きを変える速さは maxCameraRate_ まで（速いと追いつけずに外れる）
  //===========================
  const float blend = 1.0f - std::exp(-dt / (std::max)(cameraLag_, 0.001f));
  Vector3 next = Normalize(lookDir_ + (want - lookDir_) * blend);
  const float angle = std::acos(std::clamp(Dot(lookDir_, next), -1.0f, 1.0f));
  const float maxAngle = maxCameraRate_ * dt;
  if (angle > maxAngle && angle > 0.0f) {
    next = Normalize(lookDir_ + (next - lookDir_) * (maxAngle / angle));
  }
  lookDir_ = next;
}

void ClearFlight::VisitConfigValues(
    const std::function<void(const std::string &key, float &value)> &visit) {
  auto visitVector = [&](const std::string &key, Vector3 &value) {
    visit(key + ".x", value.x);
    visit(key + ".y", value.y);
    visit(key + ".z", value.z);
  };
  // 最初に通る点
  visitVector("startOffset", startOffset_);
  visit("startSpeed", startSpeed_);
  visitVector("nearPassOffset", nearPassOffset_);
  visit("sunDistance", sunDistance_);
  visitVector("diveLowOffset", diveLowOffset_);
  visitVector("nearPassAgainOffset", nearPassAgainOffset_);
  visitVector("overHeadOffset", overHeadOffset_);
  // 気ままに飛ぶ点を選ぶ帯
  auto visitRange = [&](const std::string &key, Vector2 &value) {
    visit(key + ".min", value.x);
    visit(key + ".max", value.y);
  };
  visit("midWeight", midWeight_);
  visit("nearWeight", nearWeight_);
  visit("farWeight", farWeight_);
  visit("midMinDistance", midMinDistance_);
  visit("midMaxDistance", midMaxDistance_);
  visit("farMinDistance", farMinDistance_);
  visit("farMaxDistance", farMaxDistance_);
  visit("spreadYawDeg", spreadYawDeg_);
  visit("minElevationDeg", minElevationDeg_);
  visit("maxElevationDeg", maxElevationDeg_);
  visitRange("nearSide", nearSide_);
  visitRange("nearUp", nearUp_);
  visitRange("nearForward", nearForward_);
  visit("minHopDistance", minHopDistance_);
  visit("reachRadius", reachRadius_);
  visit("targetTimeout", targetTimeout_);
  visit("floorOffset", floorOffset_);
  // 舵
  visit("turnGain", turnGain_);
  visit("maxYawRate", maxYawRate_);
  visit("maxBank", maxBank_);
  visit("bankRate", bankRate_);
  visit("pitchGain", pitchGain_);
  visit("maxPitchRate", maxPitchRate_);
  visit("maxPitch", maxPitch_);
  visit("resultMaxPitch", resultMaxPitch_);
  // 速さ
  visit("cruiseSpeed", cruiseSpeed_);
  visit("resultCruiseSpeed", resultCruiseSpeed_);
  visit("brakeStartDeg", brakeStartDeg_);
  visit("brakeFullDeg", brakeFullDeg_);
  visit("brakeSpeed", brakeSpeed_);
  visit("brakeDecel", brakeDecel_);
  visit("brakeLag", brakeLag_);
  visit("brakePitch", brakePitch_);
  visit("brakeHover", brakeHover_);
  visit("glideSpeed", glideSpeed_);
  visit("minSpeed", minSpeed_);
  visit("maxSpeed", maxSpeed_);
  visit("flapAccel", flapAccel_);
  visit("drag", drag_);
  visit("slopeAccel", slopeAccel_);
  visit("flapSpeedRange", flapSpeedRange_);
  visit("climbFlap", climbFlap_);
  visit("flapLag", flapLag_);
  // 遊び
  visit("spreadSec", spreadSec_);
  visit("rollSec", rollSec_);
  visit("rollChance", rollChance_);
  visit("rollMinSpeed", rollMinSpeed_);
  visit("rollMaxBank", rollMaxBank_);
  visit("rollMaxPitch", rollMaxPitch_);
  visitVector("bodyPivot", bodyPivot_);
  visit("bankSign", bankSign_);
  // 翼
  visit("flapStrength", flapStrength_);
  visit("glideStrength", glideStrength_);
  visit("flapFrequency", flapFrequency_);
  visit("glideFrequency", glideFrequency_);
  // カメラ
  visit("followWeight", followWeight_);
  visit("cameraLag", cameraLag_);
  visit("maxCameraRate", maxCameraRate_);
  visit("behindDot", behindDot_);
  visit("minLookPitch", minLookPitch_);
  visit("settleSec", settleSec_);
  visit("resultYawOffsetDeg", resultYawOffsetDeg_);
  visit("resultPitchOffsetDeg", resultPitchOffsetDeg_);
  visitRange("resultDistance", resultDistance_);
  visit("resultYawMarginDeg", resultYawMarginDeg_);
  visit("resultPitchMarginDeg", resultPitchMarginDeg_);
  visit("resultMinElevationDeg", resultMinElevationDeg_);
  visit("resultMaxHeight", resultMaxHeight_);
  visit("resultPassChance", resultPassChance_);
  visit("resultCandidates", resultCandidates_);
  visit("recentSec", recentSec_);
  visit("recentRadius", recentRadius_);
  visit("recentMinWeight", recentMinWeight_);
  visit("rightWeight", rightWeight_);
  visit("boardWeight", boardWeight_);
  visit("resultMinFlap", resultMinFlap_);
  visit("resultRollChance", resultRollChance_);
  visit("passAngleDeg", passAngleDeg_);
}

void ClearFlight::DrawImGui() {
#ifdef USE_IMGUI
  const char *const kWaypointNames[] = {"NearPass", "Sun", "DiveLow",
                                        "NearPassAgain", "OverHead", "Wander"};
  const char *const kBandNames[] = {"Mid", "Near", "Far"};
  ImGui::Text("Flight %.2f  [%s / %s]  dist %.0f  speed %.1f  flap %.2f  bank %.2f",
              time_, kWaypointNames[static_cast<int>(waypoint_)],
              kBandNames[static_cast<int>(lastBand_)],
              Length(position_ - cameraPos_), speed_, flap_, bank_);
  if (ImGui::TreeNode("Route")) {
    ImGui::DragFloat3("Start Offset", &startOffset_.x, 0.1f);
    ImGui::DragFloat("Start Speed", &startSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat3("Near Pass", &nearPassOffset_.x, 0.1f);
    ImGui::DragFloat("Sun Distance", &sunDistance_, 0.5f, 5.0f, 300.0f);
    ImGui::DragFloat3("Dive Low", &diveLowOffset_.x, 0.5f);
    ImGui::DragFloat3("Near Pass Again", &nearPassAgainOffset_.x, 0.1f);
    ImGui::DragFloat3("Over Head", &overHeadOffset_.x, 0.5f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Wander")) {
    ImGui::SliderFloat("Mid Weight", &midWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat("Near Weight", &nearWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat("Far Weight", &farWeight_, 0.0f, 1.0f);
    ImGui::DragFloatRange2("Mid Distance", &midMinDistance_, &midMaxDistance_,
                           0.5f, 1.0f, 500.0f);
    ImGui::DragFloatRange2("Far Distance", &farMinDistance_, &farMaxDistance_,
                           0.5f, 1.0f, 500.0f);
    ImGui::SliderFloat("Spread Yaw Deg", &spreadYawDeg_, 0.0f, 90.0f);
    ImGui::DragFloatRange2("Elevation Deg", &minElevationDeg_,
                           &maxElevationDeg_, 0.5f, -10.0f, 89.0f);
    ImGui::DragFloat2("Near Side", &nearSide_.x, 0.1f);
    ImGui::DragFloat2("Near Up", &nearUp_.x, 0.1f);
    ImGui::DragFloat2("Near Forward", &nearForward_.x, 0.1f);
    ImGui::DragFloat("Min Hop Distance", &minHopDistance_, 0.5f, 0.0f, 200.0f);
    ImGui::DragFloat("Reach Radius", &reachRadius_, 0.1f, 0.5f, 50.0f);
    ImGui::DragFloat("Target Timeout", &targetTimeout_, 0.1f, 0.5f, 30.0f);
    ImGui::DragFloat("Floor Offset", &floorOffset_, 0.1f, -100.0f, 100.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Steering")) {
    ImGui::DragFloat("Turn Gain", &turnGain_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Max Yaw Rate", &maxYawRate_, 0.01f, 0.0f, 5.0f);
    ImGui::SliderAngle("Max Bank", &maxBank_, 0.0f, 85.0f);
    ImGui::DragFloat("Bank Rate", &bankRate_, 0.01f, 0.1f, 10.0f);
    ImGui::DragFloat("Pitch Gain", &pitchGain_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Max Pitch Rate", &maxPitchRate_, 0.01f, 0.0f, 5.0f);
    ImGui::SliderAngle("Max Pitch", &maxPitch_, 0.0f, 89.0f);
    ImGui::SliderAngle("Result Max Pitch", &resultMaxPitch_, 0.0f, 89.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Speed")) {
    ImGui::DragFloat("Cruise Speed", &cruiseSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat("Result Cruise Speed", &resultCruiseSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::SeparatorText("Brake");
    ImGui::DragFloat("Brake Start Deg", &brakeStartDeg_, 0.5f, 0.0f, 180.0f);
    ImGui::DragFloat("Brake Full Deg", &brakeFullDeg_, 0.5f, 0.0f, 180.0f);
    ImGui::DragFloat("Brake Speed", &brakeSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat("Brake Decel", &brakeDecel_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat("Brake Lag", &brakeLag_, 0.01f, 0.01f, 2.0f);
    ImGui::SliderAngle("Brake Pitch", &brakePitch_, 0.0f, 60.0f);
    ImGui::SliderFloat("Brake Hover", &brakeHover_, 0.0f, 1.0f);
    ImGui::Text("brake %.2f", brake_);
    ImGui::DragFloat("Glide Speed", &glideSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat("Min Speed", &minSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::DragFloat("Max Speed", &maxSpeed_, 0.1f, 0.0f, 120.0f);
    ImGui::DragFloat("Flap Accel", &flapAccel_, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Drag", &drag_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Slope Accel", &slopeAccel_, 0.1f, 0.0f, 30.0f);
    ImGui::DragFloat("Flap Speed Range", &flapSpeedRange_, 0.1f, 0.1f, 40.0f);
    ImGui::DragFloat("Climb Flap", &climbFlap_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Flap Lag", &flapLag_, 0.01f, 0.01f, 3.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Play / Wing")) {
    ImGui::DragFloat("Spread Sec", &spreadSec_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Roll Sec", &rollSec_, 0.01f, 0.1f, 5.0f);
    ImGui::SliderFloat("Roll Chance", &rollChance_, 0.0f, 1.0f);
    ImGui::DragFloat("Roll Min Speed", &rollMinSpeed_, 0.1f, 0.0f, 80.0f);
    ImGui::SliderAngle("Roll Max Bank", &rollMaxBank_, 0.0f, 60.0f);
    ImGui::SliderAngle("Roll Max Pitch", &rollMaxPitch_, 0.0f, 60.0f);
    ImGui::DragFloat3("Body Pivot", &bodyPivot_.x, 0.05f);
    ImGui::SliderFloat("Bank Sign", &bankSign_, -1.0f, 1.0f);
    ImGui::SliderFloat("Flap Strength", &flapStrength_, 0.0f, 1.0f);
    ImGui::SliderFloat("Glide Strength", &glideStrength_, 0.0f, 1.0f);
    ImGui::DragFloat("Flap Frequency", &flapFrequency_, 0.01f, 0.1f, 5.0f);
    ImGui::DragFloat("Glide Frequency", &glideFrequency_, 0.01f, 0.1f, 5.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Follow Camera")) {
    ImGui::SliderFloat("Follow Weight", &followWeight_, 0.0f, 1.0f);
    ImGui::DragFloat("Camera Lag", &cameraLag_, 0.01f, 0.01f, 5.0f);
    ImGui::DragFloat("Max Camera Rate", &maxCameraRate_, 0.01f, 0.0f, 5.0f);
    ImGui::SliderFloat("Behind Dot", &behindDot_, -1.0f, 1.0f);
    ImGui::SliderAngle("Min Look Pitch", &minLookPitch_, -30.0f, 60.0f);
    ImGui::DragFloat("Settle Sec", &settleSec_, 0.01f, 0.0f, 5.0f);
    ImGui::SliderFloat("Result Yaw Offset Deg", &resultYawOffsetDeg_, -45.0f, 45.0f);
    ImGui::SliderFloat("Result Pitch Offset Deg", &resultPitchOffsetDeg_, -30.0f, 30.0f);
    ImGui::SeparatorText("Result");
    ImGui::Text("Frame Half Yaw %.1f / Pitch %.1f deg", frameHalfYawDeg_,
                frameHalfPitchDeg_);
    ImGui::SliderFloat("Pass Angle Deg", &passAngleDeg_, 0.0f, 180.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Result Flight")) {
    ImGui::DragFloat2("Result Distance", &resultDistance_.x, 0.5f);
    ImGui::DragFloat("Yaw Margin Deg", &resultYawMarginDeg_, 0.5f, 0.0f, 90.0f);
    ImGui::DragFloat("Pitch Margin Deg", &resultPitchMarginDeg_, 0.5f, 0.0f, 60.0f);
    ImGui::DragFloat("Min Elevation Deg", &resultMinElevationDeg_, 0.5f, -10.0f, 60.0f);
    ImGui::DragFloat("Max Height", &resultMaxHeight_, 0.5f, 0.0f, 200.0f);
    ImGui::SliderFloat("Pass Chance", &resultPassChance_, 0.0f, 1.0f);
    ImGui::SliderFloat("Candidates", &resultCandidates_, 1.0f, 40.0f, "%.0f");
    ImGui::DragFloat("Recent Sec", &recentSec_, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Recent Radius", &recentRadius_, 0.1f, 0.1f, 100.0f);
    ImGui::SliderFloat("Recent Min Weight", &recentMinWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat("Right Weight", &rightWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat("Board Weight", &boardWeight_, 0.0f, 1.0f);
    ImGui::SliderFloat("Min Flap", &resultMinFlap_, 0.0f, 1.0f);
    ImGui::SliderFloat("Roll Chance", &resultRollChance_, 0.0f, 1.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Result Stats")) {
    const ResultStats &stats = resultStats_;
    const float total = (std::max)(stats.total, 0.001f);
    const float inFrame = stats.largeSec + stats.mediumSec + stats.smallSec;
    ImGui::Text("Time %.1f s", stats.total);
    ImGui::Text("Visible %3.0f%%  (Large %3.0f%%  Medium %3.0f%%  Small %3.0f%%)",
                inFrame / total * 100.0f, stats.largeSec / total * 100.0f,
                stats.mediumSec / total * 100.0f, stats.smallSec / total * 100.0f);
    ImGui::Text("Behind Board %3.0f%%", stats.boardSec / total * 100.0f);
    ImGui::Text("Absent Now %.1f s  Longest %.1f s", stats.absent, stats.maxAbsent);
    // 行き先の種類ごと：向かっていた秒、そのうち板の裏・画面の外の秒
    const char *const kLegNames[kLegCount] = {"In", "Out", "Pass"};
    ImGui::Text("%-8s %7s %7s %7s", "Leg", "Time", "Board", "Out");
    for (int i = 0; i < kLegCount; ++i) {
      ImGui::Text("%-8s %6.1fs %6.1fs %6.1fs", kLegNames[i], stats.legTime[i],
                  stats.legBoard[i], stats.legOut[i]);
    }
    ImGui::Text("Out: Behind Cam %.1fs  Left %.1fs  Right %.1fs  Above %.1fs  Below %.1fs",
                stats.outWhere[0], stats.outWhere[1], stats.outWhere[2],
                stats.outWhere[3], stats.outWhere[4]);
    if (ImGui::Button("Reset Stats")) {
      resultStats_ = {};
    }
    ImGui::DragFloat("Measure Span", &measureSpan_, 0.1f, 0.1f, 50.0f);
    ImGui::SliderFloat("Large Ratio", &largeRatio_, 0.0f, 1.0f);
    ImGui::SliderFloat("Small Ratio", &smallRatio_, 0.0f, 1.0f);
    ImGui::SliderFloat("Board Left NDC", &boardLeftNdc_, -1.0f, 1.0f);
    ImGui::TreePop();
  }
#endif // USE_IMGUI
}
