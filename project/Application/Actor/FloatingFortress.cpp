#include "Actor/FloatingFortress.h"
#include "Audio/SoundManager.h"
#include "Camera/RailCamera.h"
#include "Effect/EffectManager.h"
#include "Effect/FractureCache.h"
#include "Effect/FractureDebris.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include "Math/Matrix4x4.h"
#include <limits>
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include <algorithm>
#include <cmath>
#include <numbers>

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
constexpr Vector4 kFlameColor = {1.0f, 0.5f, 0.15f, 1.0f};    // 推進器の火の色
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
                                  const RailCamera *railCamera,
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

  heldChunks_.clear();

  // 本体の箱ごとの割る数を決める。体積が一番大きい箱だけ、大きい箱用の数にする
  const BigEnemyDeath::Config &deathConfig = EffectManager::GetInstance()->GetDeathConfig();
  size_t largestIndex = 0;
  for (size_t i = 1; i < bodyBoxes_.size(); ++i) {
    const Vector3 &h = bodyBoxes_[i].halfSize;
    const Vector3 &largest = bodyBoxes_[largestIndex].halfSize;
    if (h.x * h.y * h.z > largest.x * largest.y * largest.z) {
      largestIndex = i;
    }
  }
  chunkSeedCounts_.assign(bodyBoxes_.size(), deathConfig.chunkSeedCount);
  chunkSeedCounts_[largestIndex] = deathConfig.chunkLargeSeedCount;

  // 大爆発で本体の箱を割る形を作っておく（同じ箱のモデル・同じ数なら作成済みなので何もしない）
  for (size_t i = 0; i < boxes_.size(); ++i) {
    FractureCache::GetInstance()->Prebuild(boxes_[i]->GetModel(), chunkSeedCounts_[i]);
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
  case State::Enduring:
    UpdateEnduring();
    break;
  case State::Dying:
    UpdateDying();
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

void FloatingFortress::FollowPathAfterDeath() {
  deadTime_ += kFrameTime_;
  const bool isFalling = state_ == State::Dying;
  if (isFalling) {
    fallTime_ += kFrameTime_;
    deadTiltRate_ = (std::min)(deadTiltRate_ + kFrameTime_ / kDeadTiltSeconds_, 1.0f);
  }

  // カメラの t ではなく自分の時計で道筋を進む。割れ始めてからは推力を失い、進む速さが減っていく
  const float thrust = isFalling ? std::exp(-kDeadThrustLossRate_ * fallTime_) : 1.0f;
  const float n = static_cast<float>(path_.size() - 1);
  deadPathU_ += kPathTPerSecond_ * thrust * kFrameTime_ / pathSpanT_ * n;
  if (deadPathU_ < n) {
    const Vector3 next = PathPoint(deadPathU_);
    pathVelocity_ = (next - pathPosition_) * (1.0f / kFrameTime_);
    pathPosition_ = next;
  } else {
    // 道筋の終わりを過ぎたら、最後の向きのまままっすぐ進む
    const float previousThrust =
        isFalling ? std::exp(-kDeadThrustLossRate_ * (fallTime_ - kFrameTime_)) : 1.0f;
    pathVelocity_ = pathVelocity_ * (thrust / previousThrust);
    pathPosition_ = pathPosition_ + pathVelocity_ * kFrameTime_;
  }

  // 割れ始めてからは、道筋の上の位置から重力で落ちた分だけ下に置く
  const float fallDepth = isFalling ? 0.5f * kDeadGravity_ * fallTime_ * fallTime_ : 0.0f;
  const Vector3 next = pathPosition_ - Vector3{0.0f, fallDepth, 0.0f};
  velocity_ = (next - position_) * (1.0f / kFrameTime_);
  position_ = next;

  // 向きを、横に動く向きへだんだん回す
  const float horizontalSpeed = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
  if (horizontalSpeed > kDeadTurnMinSpeed_) {
    yaw_ = LerpAngle(yaw_, std::atan2(velocity_.x, velocity_.z), kDeadTurnFollow_);
  }

  // 傾きの目標を、倒されたときの傾きから落ちる向きの傾きへ傾いた割合だけ寄せ、左右と前後のふらつきを足す
  constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
  targetRoll_ = deadRollStart_ + (deadRollBase_ - deadRollStart_) * deadTiltRate_ +
                kDeadWobbleRoll_ * std::sin(kTwoPi * kDeadWobbleRollHz_ * deadTime_);
  targetPitch_ = deadPitchStart_ + (kFallPitch_ - deadPitchStart_) * deadTiltRate_ +
                 kDeadWobblePitch_ * std::sin(kTwoPi * kDeadWobblePitchHz_ * deadTime_);

  // 割れ目が走る間は、走った割合に合わせて大きくなる震えを、見た目の位置と傾きにだけ足す
  const float crackSeconds = EffectManager::GetInstance()->GetDeathConfig().crackSeconds;
  if (isFalling && fallTime_ < crackSeconds) {
    const float amount = fallTime_ / (std::max)(crackSeconds, 1e-3f);
    const float phase = kTwoPi * kCrackShakeHz_ * fallTime_;
    // 3 つの向きと 2 つの傾きで、周期と始まりをずらす
    shakeOffset_ = Vector3{std::sin(phase), std::sin(phase * 1.3f + 1.0f),
                           std::sin(phase * 0.8f + 2.0f)} *
                   (kCrackShakeDistance_ * amount);
    shakeRoll_ = kCrackShakeAngle_ * amount * std::sin(phase * 1.1f + 0.5f);
    shakePitch_ = kCrackShakeAngle_ * amount * std::sin(phase * 0.9f + 1.5f);
  } else {
    shakeOffset_ = {0.0f, 0.0f, 0.0f};
    shakeRoll_ = 0.0f;
    shakePitch_ = 0.0f;
  }
  UpdateTilt();
  BuildFrame();
  BuildDeathBoxes();
}

void FloatingFortress::BuildDeathBoxes() {
  // 箱のモデルの拡大は除き、回転と位置だけにする
  deathBoxes_.clear();
  for (size_t i = 0; i < bodyBoxes_.size(); ++i) {
    FractureDebris *chunks = i < heldChunks_.size() ? heldChunks_[i].get() : nullptr;
    deathBoxes_.push_back({Multiply(MakeTranslateMatrix(bodyBoxes_[i].center), frame_),
                           bodyBoxes_[i].halfSize, chunks});
  }
}

void FloatingFortress::UpdateEnduring() {
  FollowPathAfterDeath();
  enduringTimer_ += kFrameTime_;
  death_.UpdateDamage(deathBoxes_, velocity_);

  // 見える位置に来て、飛び散る塊がカメラに近づきすぎないなら割れ始める
  // そうならなくても、耐える時間を過ぎるか道筋の終わりを過ぎたら割れ始める
  const float n = static_cast<float>(path_.size() - 1);
  if ((IsInDeathView() && KeepsClearOfCamera()) || enduringTimer_ >= kMaxEnduringSeconds_ ||
      deadPathU_ >= n) {
    state_ = State::Dying;
    StartDeath(lastPartPosition_);
  }
}

void FloatingFortress::UpdateDying() {
  FollowPathAfterDeath();
  death_.Update(frame_, deathBoxes_, velocity_);

  // 大爆発を、砦の出来事としてシーンへ伝える（ヒットストップは倒された瞬間に伝えてある）
  for (BigEnemyDeath::Event event : death_.TakeEvents()) {
    if (event == BigEnemyDeath::Event::Blast) {
      SoundManager::GetInstance()->PlaySE("boss_explosion");
      ReleaseChunks();
      events_.push_back(Event::Exploded);
    }
  }
  if (death_.IsFinished()) {
    state_ = State::Finished;
  }
}

bool FloatingFortress::KeepsClearOfCamera() const {
  if (!railCamera_) {
    return true;
  }
  const BigEnemyDeath::Config &config = EffectManager::GetInstance()->GetDeathConfig();
  const float n = static_cast<float>(path_.size() - 1);
  const float uPerSecond = kPathTPerSecond_ / pathSpanT_ * n; // 1 秒に進む道筋の点の番号
  const float cameraTPerSecond = railCamera_->GetCurrentSpeed();
  const float crackSeconds = config.crackSeconds;
  const float drag = (std::max)(config.chunkDrag, 1e-3f);

  // 割れ始めてから s 秒の、道筋の点の番号と本体の位置（推力を失って進みが減り、重力で落ちる）
  const auto uAt = [&](float s) {
    return deadPathU_ + uPerSecond * (1.0f - std::exp(-kDeadThrustLossRate_ * s)) / kDeadThrustLossRate_;
  };
  const auto bodyAt = [&](float s) {
    return PathPoint(uAt(s)) - Vector3{0.0f, 0.5f * kDeadGravity_ * s * s, 0.0f};
  };

  // 大爆発で放す位置と速さ
  const Vector3 release = bodyAt(crackSeconds);
  const Vector3 releaseVelocity =
      (bodyAt(crackSeconds + kFrameTime_) - release) * (1.0f / kFrameTime_);

  for (float s = 0.0f; s <= crackSeconds + config.chunkLifetime; s += kClearanceStep_) {
    Vector3 point;
    if (s <= crackSeconds) {
      point = bodyAt(s);
    } else {
      // 速さは空気の抵抗で 1 秒に drag の割合ずつ減り、落ちる速さは重力 / drag に近づく
      const float a = s - crackSeconds;
      const float travel = (1.0f - std::exp(-drag * a)) / drag;
      point = release + releaseVelocity * travel +
              Vector3{0.0f, -(config.chunkGravity / drag) * (a - travel), 0.0f};
    }
    Vector3 camera = railCamera_->CalcPosition(railCamera_->GetT() + cameraTPerSecond * s);
    camera.y += RailCamera::kRailHeight_;
    if (Length(point - camera) < kDeathMinClearance_) {
      return false;
    }
  }
  return true;
}

bool FloatingFortress::IsInDeathView() const {
  if (!railCamera_) {
    return true;
  }
  // カメラから見た奥行きが足りなければ、まだ近すぎる
  const Vector3 inView = TransformPoint(position_, railCamera_->GetViewMatrix());
  if (inView.z < kDeathViewDistance_) {
    return false;
  }
  // 中心が画面のふちから kDeathViewMarginUV_ より内側にあるか
  const Vector2 uv = EffectManager::WorldToScreenUV(position_, railCamera_);
  return uv.x >= kDeathViewMarginUV_ && uv.x <= 1.0f - kDeathViewMarginUV_ &&
         uv.y >= kDeathViewMarginUV_ && uv.y <= 1.0f - kDeathViewMarginUV_;
}

void FloatingFortress::UpdateTilt() {
  roll_ += (targetRoll_ - roll_) * kTiltFollow_;
  pitch_ += (targetPitch_ - pitch_) * kTiltFollow_;
}

void FloatingFortress::BuildFrame() {
  // 震えは見た目の位置と傾きにだけ足し、動きの計算（位置と速さ）には入れない
  const Vector3 shownPosition = position_ + shakeOffset_;
  const float shownPitch = pitch_ + shakePitch_;
  const float shownRoll = roll_ + shakeRoll_;
  root_->SetTranslation(shownPosition);
  root_->SetRotation({0.0f, yaw_, 0.0f});
  tilt_->SetRotation({shownPitch, 0.0f, shownRoll});
  frame_ = Multiply(MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{shownPitch, 0.0f, shownRoll},
                                     Vector3{0.0f, 0.0f, 0.0f}),
                    MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{0.0f, yaw_, 0.0f},
                                     shownPosition));
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
  if (state_ == State::Finished || !death_.IsBodyVisible()) {
    return;
  }
  // 死亡演出が始まったら、本体の箱の代わりにつかんでいる塊を描く
  if (death_.IsStarted()) {
    for (auto &chunks : heldChunks_) {
      if (chunks) {
        chunks->Draw3D();
      }
    }
    return;
  }
  for (auto &box : boxes_) {
    box->Draw();
  }
}

void FloatingFortress::AppendOccluders(std::vector<Matrix4x4> &worldToBoxes) const {
  if (state_ == State::Finished || !death_.IsBodyVisible()) {
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

  // 全部壊れたら、道筋を進みながら耐える。割れ目は最後の部品の所から走る
  if (aliveParts_ <= 0 && state_ == State::Fly) {
    state_ = State::Enduring;
    deadRollStart_ = targetRoll_;
    deadPitchStart_ = targetPitch_;
    deadRollBase_ = (targetRoll_ < 0.0f ? -1.0f : 1.0f) * kFallRoll_;
    deadTiltRate_ = 0.0f;
    deadTime_ = 0.0f;
    fallTime_ = 0.0f;
    pathPosition_ = position_;
    pathVelocity_ = velocity_;
    deadPathU_ = (railCamera_->GetT() - spawnT_) / pathSpanT_ * static_cast<float>(path_.size() - 1);
    enduringTimer_ = 0.0f;
    lastPartPosition_ = spec.localPosition;
    events_.push_back(Event::Killed);
  }
}

void FloatingFortress::StartDeath(const Vector3 &lastPartPosition) {
  // 本体の箱それぞれで、部品の位置を箱の中へ押し込んだ点（箱の表面で一番近い点）を求め、一番近い箱の点を使う
  float bestDistance = (std::numeric_limits<float>::max)();
  Vector3 bestPoint = lastPartPosition;
  Vector3 bestNormal = {0.0f, 1.0f, 0.0f};
  for (const BodyBox &b : bodyBoxes_) {
    const Vector3 local = lastPartPosition - b.center;
    const Vector3 clamped = {std::clamp(local.x, -b.halfSize.x, b.halfSize.x),
                             std::clamp(local.y, -b.halfSize.y, b.halfSize.y),
                             std::clamp(local.z, -b.halfSize.z, b.halfSize.z)};
    const Vector3 outside = local - clamped;
    const float distance = Length(outside);
    if (distance >= bestDistance) {
      continue;
    }
    bestDistance = distance;
    bestPoint = clamped + b.center;
    // 外向きは、箱からはみ出した量が一番大きい軸の面の向き
    const Vector3 a = {std::abs(outside.x), std::abs(outside.y), std::abs(outside.z)};
    if (a.x >= a.y && a.x >= a.z && a.x > 0.0f) {
      bestNormal = {outside.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f};
    } else if (a.y >= a.z && a.y > 0.0f) {
      bestNormal = {0.0f, outside.y > 0.0f ? 1.0f : -1.0f, 0.0f};
    } else if (a.z > 0.0f) {
      bestNormal = {0.0f, 0.0f, outside.z > 0.0f ? 1.0f : -1.0f};
    } else {
      bestNormal = {0.0f, 1.0f, 0.0f};
    }
  }
  BreakIntoChunks();
  death_.Start(bestPoint, bestNormal);
}

std::vector<FloatingFortress::Event> FloatingFortress::TakeEvents() {
  std::vector<Event> events;
  events.swap(events_);
  return events;
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
    EmitThrusterFlame(i);
  }
}

void FloatingFortress::EmitThrusterFlame(int index) {
  const PartSpec &spec = GetPartSpecs()[index];
  const Vector3 local =
      spec.localPosition + Vector3{0.0f, 0.0f, -(spec.halfSize.z + kFlameBack_)};
  EffectManager::GetInstance()->PlayEffect(EffectType::HitSpark, TransformPoint(local, frame_),
                                           kFlameColor, kFlameScale_);
}

void FloatingFortress::BreakIntoChunks() {
  heldChunks_.clear();
  Object3dRenderer *renderer = PrefabManager::GetInstance()->GetObject3dRenderer();
  if (!renderer) {
    return;
  }
  const BigEnemyDeath::Config &config = EffectManager::GetInstance()->GetDeathConfig();

  // 塊の外側は体の色、割れ口は火の色に光らせる（割れ目が開くにつれて明るくなる）
  FractureDebris::Look look;
  look.color = {kBodyColor.x * config.chunkColorRate, kBodyColor.y * config.chunkColorRate,
                kBodyColor.z * config.chunkColorRate, kBodyColor.w};
  look.capGlowColor = Vector4{config.chunkCapColor.x * config.chunkCapIntensity,
                              config.chunkCapColor.y * config.chunkCapIntensity,
                              config.chunkCapColor.z * config.chunkCapIntensity, 1.0f};

  // 放したあとの飛び方
  FractureDebris::Params params;
  params.explosionSpeed = config.chunkSpeed;
  params.gravity = {0.0f, -config.chunkGravity, 0.0f};
  params.maxAngularSpeed = config.chunkSpin;
  params.lifetime = config.chunkLifetime;
  params.drag = config.chunkDrag;

  // 放したあとの燃え方（割れ口は暗く冷め、割れ口から炎と黒煙を出す）
  FractureDebris::Burning burning;
  burning.coolSeconds = config.chunkCoolSeconds;
  burning.cooledColor = {(*look.capGlowColor).x * config.chunkCooledRate,
                         (*look.capGlowColor).y * config.chunkCooledRate,
                         (*look.capGlowColor).z * config.chunkCooledRate, 1.0f};
  burning.flameRate = config.chunkFlameRate;
  burning.smokeRate = config.chunkSmokeRate;
  burning.trailInherit = config.chunkTrailInherit;

  // 箱ごとに、箱の今の位置・向きへ、箱の大きさで塊を並べる（割れる形がなければその箱は nullptr）
  for (size_t i = 0; i < bodyBoxes_.size(); ++i) {
    const FracturePieceSet *pieceSet =
        FractureCache::GetInstance()->Find(boxes_[i]->GetModel(), chunkSeedCounts_[i]);
    if (!pieceSet) {
      heldChunks_.push_back(nullptr);
      continue;
    }
    heldChunks_.push_back(std::make_unique<FractureDebris>(
        renderer, *pieceSet, Multiply(MakeTranslateMatrix(bodyBoxes_[i].center), frame_),
        bodyBoxes_[i].halfSize * 2.0f, look, params));
    heldChunks_.back()->SetBurning(burning);
  }
}

void FloatingFortress::ReleaseChunks() {
  // 放した塊は ActorManager に渡し、寿命まで飛ばしてもらう
  for (auto &chunks : heldChunks_) {
    if (!chunks) {
      continue;
    }
    chunks->Release(velocity_);
    ActorManager::GetInstance()->AddActor(std::move(chunks));
  }
  heldChunks_.clear();
}
