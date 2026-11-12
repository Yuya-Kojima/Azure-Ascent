#include "Actor/FloatingFortress.h"
#include "Audio/SoundManager.h"
#include "Camera/RailCamera.h"
#include "Effect/EffectManager.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include <algorithm>
#include <cmath>

namespace {
// 本体の箱（砦の中の中心・半分の大きさ m）。本体・甲板・砲座の台の 3 個
constexpr Vector3 kBodyCenter = {0.0f, 0.0f, 0.0f};
constexpr Vector3 kBodyHalf = {16.0f, 7.0f, 20.0f};
constexpr Vector3 kDeckCenter = {0.0f, 9.0f, 0.0f};
constexpr Vector3 kDeckHalf = {10.0f, 2.0f, 14.0f};
constexpr Vector3 kTurretMountCenter = {0.0f, -10.0f, 0.0f};
constexpr Vector3 kTurretMountHalf = {4.0f, 3.0f, 12.0f};

const std::string kBoxModelPath = "__builtin_box";           // エンジン組み込みの一辺 1 の箱
constexpr Vector4 kBodyColor = {0.12f, 0.125f, 0.14f, 1.0f}; // 本体の色（区間 2 の塔と同じ濃い灰）
constexpr Vector4 kExplosionColor = {1.0f, 0.55f, 0.2f, 1.0f}; // 雲海に沈む所の爆発の色
constexpr Vector4 kFlameColor = {1.0f, 0.5f, 0.15f, 1.0f};    // 推進器の火の色
constexpr float kTwoPi = 6.2831853f;                          // 1 周の角度（ラジアン）
} // namespace

FloatingFortress::FloatingFortress() = default;
FloatingFortress::~FloatingFortress() = default;

const std::array<FloatingFortress::PartSpec, FloatingFortress::kPartCount> &
FloatingFortress::GetPartSpecs() {
  // 上の 4 個は気嚢、下の真ん中の前後 2 個は砲座、後ろの左右 2 個は推進器
  static const std::array<PartSpec, kPartCount> kSpecs = {{
      {EnemyPart::Role::Balloon, {-11.0f, 22.0f, 12.0f}, {5.0f, 8.0f, 5.0f}},
      {EnemyPart::Role::Balloon, {-11.0f, 22.0f, -12.0f}, {5.0f, 8.0f, 5.0f}},
      {EnemyPart::Role::Balloon, {11.0f, 22.0f, 12.0f}, {5.0f, 8.0f, 5.0f}},
      {EnemyPart::Role::Balloon, {11.0f, 22.0f, -12.0f}, {5.0f, 8.0f, 5.0f}},
      {EnemyPart::Role::Turret, {0.0f, -15.0f, 13.0f}, {2.0f, 2.0f, 2.0f}},
      {EnemyPart::Role::Turret, {0.0f, -15.0f, -13.0f}, {2.0f, 2.0f, 2.0f}},
      {EnemyPart::Role::Thruster, {-10.0f, -10.0f, -16.0f}, {3.0f, 3.0f, 3.0f}},
      {EnemyPart::Role::Thruster, {10.0f, -10.0f, -16.0f}, {3.0f, 3.0f, 3.0f}},
  }};
  return kSpecs;
}

void FloatingFortress::Initialize(Object3dRenderer *renderer,
                                  RailCamera *railCamera,
                                  const std::vector<Vector3> &path,
                                  float spawnT, float pathSeconds,
                                  std::optional<float> heading) {
  path_ = path;
  railCamera_ = railCamera;
  spawnT_ = spawnT;
  pathSpanT_ = (std::max)(pathSeconds * kPathTPerSecond_, 1e-3f);
  heading_ = heading;
  state_ = State::Fly;
  parts_.fill(nullptr);
  aliveParts_ = 0;

  root_ = std::make_unique<Object3d>();
  root_->Initialize(renderer);
  tilt_ = std::make_unique<Object3d>();
  tilt_->Initialize(renderer);
  tilt_->SetParent(root_.get());

  bodyBoxes_ = {{kBodyCenter, kBodyHalf},
                {kDeckCenter, kDeckHalf},
                {kTurretMountCenter, kTurretMountHalf}};
  boxes_.clear();
  for (const BodyBox &b : bodyBoxes_) {
    auto obj = std::make_unique<Object3d>();
    obj->Initialize(renderer);
    obj->SetModel(kBoxModelPath);
    obj->SetTranslation(b.center);
    obj->SetScale(b.halfSize * 2.0f);
    obj->SetColor(kBodyColor);
    obj->SetSpecularScale(0.0f);
    obj->SetParent(tilt_.get());
    boxes_.push_back(std::move(obj));
  }

  if (!path_.empty()) {
    position_ = path_.front();
  }
  UpdateFly();
}

void FloatingFortress::SetPart(int index, EnemyPart *part) {
  if (index < 0 || index >= kPartCount) {
    return;
  }
  if (!parts_[index] && part) {
    aliveParts_++;
  }
  parts_[index] = part;
}

Vector3 FloatingFortress::PathPoint(float u) const {
  const int n = static_cast<int>(path_.size()) - 1;
  if (n <= 0) {
    return path_.empty() ? Vector3{0.0f, 0.0f, 0.0f} : path_.front();
  }
  u = std::clamp(u, 0.0f, static_cast<float>(n));
  const int i = (std::min)(static_cast<int>(u), n - 1);
  const float t = u - static_cast<float>(i);
  const Vector3 &p0 = path_[(std::max)(i - 1, 0)];
  const Vector3 &p1 = path_[i];
  const Vector3 &p2 = path_[i + 1];
  const Vector3 &p3 = path_[(std::min)(i + 2, n)];
  const float t2 = t * t;
  const float t3 = t2 * t;
  return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
          (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) *
         0.5f;
}

void FloatingFortress::Update() {
  switch (state_) {
  case State::Fly:
    UpdateFly();
    break;
  case State::Fall:
    UpdateFall();
    break;
  case State::Sink:
    UpdateSink();
    break;
  case State::Finished:
    break;
  }
}

void FloatingFortress::UpdateFly() {
  if (!railCamera_ || path_.empty()) {
    return;
  }
  const float n = static_cast<float>(path_.size() - 1);
  const float u = (railCamera_->GetT() - spawnT_) / pathSpanT_ * n;

  // 道筋の終わりまで残った部品は、砦と一緒に静かに消える
  if (u >= n) {
    for (EnemyPart *&part : parts_) {
      if (part) {
        part->Vanish();
        part = nullptr;
      }
    }
    aliveParts_ = 0;
    state_ = State::Finished;
    return;
  }

  const Vector3 next = PathPoint(u);
  if (hasPosition_) {
    velocity_ = (next - position_) * (1.0f / kFrameTime_);
  }
  position_ = next;
  hasPosition_ = true;

  if (heading_) {
    yaw_ = *heading_;
  } else {
    const Vector3 d = PathPoint(u + kHeadingStep_) - PathPoint(u - kHeadingStep_);
    if (std::abs(d.x) + std::abs(d.z) > 1e-6f) {
      yaw_ = std::atan2(d.x, d.z);
    }
  }

  // カメラの頭上に来たら（レールの前の向きの距離が kPassOverDistance_ より近づいたら）、砲座が撃ってよくなる
  const Vector3 toFortress = position_ - railCamera_->GetRailPosition();
  if (Dot(toFortress, railCamera_->GetRailForward()) < kPassOverDistance_) {
    hasPassedOver_ = true;
  }

  UpdateTilt();
  EmitThrusterFlames();
  BuildFrame();
}

void FloatingFortress::UpdateFall() {
  velocity_.x *= kFallDrag_;
  velocity_.z *= kFallDrag_;
  velocity_.y -= kFallGravity_ * kFrameTime_;
  position_ = position_ + velocity_ * kFrameTime_;
  UpdateTilt();
  BuildFrame();
  if (position_.y < kCloudSeaY_) {
    Explode();
    state_ = State::Sink;
    sinkTimer_ = 0.0f;
  }
}

void FloatingFortress::UpdateSink() {
  velocity_.x *= kFallDrag_;
  velocity_.z *= kFallDrag_;
  velocity_.y -= kFallGravity_ * kFrameTime_;
  position_ = position_ + velocity_ * kFrameTime_;
  BuildFrame();
  sinkTimer_ += kFrameTime_;
  if (sinkTimer_ >= kSinkSeconds_) {
    state_ = State::Finished;
  }
}

void FloatingFortress::UpdateTilt() {
  roll_ += (targetRoll_ - roll_) * kTiltFollow_;
  pitch_ += (targetPitch_ - pitch_) * kTiltFollow_;
}

void FloatingFortress::BuildFrame() {
  root_->SetTranslation(position_);
  root_->SetRotation({0.0f, yaw_, 0.0f});
  tilt_->SetRotation({pitch_, 0.0f, roll_});
  frame_ = Multiply(MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{pitch_, 0.0f, roll_},
                                     Vector3{0.0f, 0.0f, 0.0f}),
                    MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{0.0f, yaw_, 0.0f},
                                     position_));
  UpdateTransform();
}

void FloatingFortress::UpdateTransform() {
  if (!root_) {
    return;
  }
  root_->Update();
  tilt_->Update();
  for (auto &box : boxes_) {
    box->Update();
  }
}

void FloatingFortress::Draw() {
  if (state_ == State::Finished) {
    return;
  }
  for (auto &box : boxes_) {
    box->Draw();
  }
}

void FloatingFortress::AppendOccluders(std::vector<Matrix4x4> &worldToBoxes) const {
  if (state_ == State::Finished) {
    return;
  }
  // 箱のモデルは -0.5〜0.5 の立方体なので、箱のワールド行列の逆のあとに 2 倍して -1〜1 にそろえる
  const Matrix4x4 toUnitCube = MakeScaleMatrix({2.0f, 2.0f, 2.0f});
  for (const BodyBox &b : bodyBoxes_) {
    const Matrix4x4 boxWorld = Multiply(
        MakeAffineMatrix(b.halfSize * 2.0f, Vector3{0.0f, 0.0f, 0.0f}, b.center), frame_);
    worldToBoxes.push_back(Multiply(Inverse(boxWorld), toUnitCube));
  }
}

Vector3 FloatingFortress::GetPartRotation(int) const {
  return {pitch_, yaw_, roll_};
}

bool FloatingFortress::CanPartFire() const {
  return state_ == State::Fly && hasPassedOver_;
}

void FloatingFortress::OnPartDestroyed(int index) {
  if (index < 0 || index >= kPartCount || !parts_[index]) {
    return;
  }
  parts_[index] = nullptr;
  aliveParts_--;

  // 気嚢が壊れたら、その側へ傾く（右が下がるのは z 回転の負、前が下がるのは x 回転の正）
  const PartSpec &spec = GetPartSpecs()[index];
  if (spec.role == EnemyPart::Role::Balloon) {
    targetRoll_ -= (spec.localPosition.x > 0.0f ? 1.0f : -1.0f) * kBalloonRoll_;
    targetPitch_ += (spec.localPosition.z > 0.0f ? 1.0f : -1.0f) * kBalloonPitch_;
  }

  // 全部壊れたら、傾いている側へさらに傾きながら雲海へ落ちる
  if (aliveParts_ <= 0 && state_ == State::Fly) {
    state_ = State::Fall;
    targetRoll_ = (targetRoll_ < 0.0f ? -1.0f : 1.0f) * kFallRoll_;
    targetPitch_ = kFallPitch_;
  }
}

void FloatingFortress::Explode() {
  for (int i = 0; i < kExplosionCount_; ++i) {
    // 砦の中で、本体の周りに輪のように散らした所で爆発させる
    const float angle = kTwoPi * static_cast<float>(i) / kExplosionCount_;
    const Vector3 local = {std::cos(angle) * kExplosionSpread_, 0.0f,
                           std::sin(angle) * kExplosionSpread_};
    EffectManager::GetInstance()->PlayEffect(
        EffectType::EnemyDeath, TransformPoint(local, frame_), kExplosionColor,
        kExplosionScale_);
  }
  EffectManager::GetInstance()->PlayShockwave(position_);
  SoundManager::GetInstance()->PlaySE("boss_explosion");
  if (railCamera_) {
    railCamera_->Shake(kShakeIntensity_, kShakeSeconds_);
  }
}

void FloatingFortress::EmitThrusterFlames() {
  if (Length(velocity_) < kThrustSpeed_) {
    flameTimer_ = 0;
    return;
  }
  if (flameTimer_-- > 0) {
    return;
  }
  flameTimer_ = kFlameInterval_;
  const auto &specs = GetPartSpecs();
  for (int i = 0; i < kPartCount; ++i) {
    if (specs[i].role != EnemyPart::Role::Thruster || !parts_[i]) {
      continue;
    }
    const Vector3 local = specs[i].localPosition +
                          Vector3{0.0f, 0.0f, -(specs[i].halfSize.z + kFlameBack_)};
    EffectManager::GetInstance()->PlayEffect(
        EffectType::HitSpark, TransformPoint(local, frame_), kFlameColor, kFlameScale_);
  }
}
