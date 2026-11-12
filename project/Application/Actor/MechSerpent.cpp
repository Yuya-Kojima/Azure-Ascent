#include "Actor/MechSerpent.h"
#include "Actor/InterceptCue.h"
#include "Actor/Player.h"
#include "Audio/SoundManager.h"
#include "Camera/RailCamera.h"
#include "Effect/EffectManager.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include <algorithm>
#include <cmath>

namespace {
const std::string kBoxModelPath = "__builtin_box";                 // エンジン組み込みの一辺 1 の箱
constexpr Vector4 kBodyColor = {0.03f, 0.03f, 0.035f, 1.0f};       // 体の節の色（黒に近い機械の色）
constexpr Vector4 kCoreDimColor = {0.45f, 0.06f, 0.05f, 1.0f};     // 溜める前の核の脈のいちばん暗い色
constexpr Vector4 kCoreIdleColor = {1.1f, 0.16f, 0.12f, 1.0f};     // 溜める前の核の脈のいちばん明るい色
constexpr Vector4 kCoreGlowColor = {2.4f, 0.35f, 0.3f, 1.0f};      // 溜まり切った光る節の色（1 を超える明るさで光らせる赤）
constexpr Vector4 kCoreBeatColor = {4.0f, 0.8f, 0.5f, 1.0f};       // 拍の瞬間の光る節の色
constexpr Vector4 kCoreWhiteHotColor = {4.0f, 3.2f, 2.8f, 1.0f};   // 溜まり切る直前（鳴りっぱなしの間）の光る節の色
constexpr Vector4 kCoreFlashColor = {4.0f, 4.0f, 4.0f, 1.0f};      // 溜め始めに光る節が光る白
constexpr Vector4 kWarnColor = {2.5f, 0.6f, 0.25f, 1.0f};          // 前触れの灯りのいちばん明るい色
constexpr Vector4 kSparkColor = {1.0f, 0.6f, 0.2f, 1.0f};          // 光る節を撃たれたときの火花の色
constexpr Vector4 kElectricColor = {1.4f, 1.2f, 2.0f, 1.0f};       // 倒された体がまとう電気の火花の色（紫がかった白）
constexpr Vector4 kExplosionColor = {1.0f, 0.55f, 0.2f, 1.0f};     // 節の爆発の色
constexpr Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
constexpr float kWarnPulseBase = 0.55f;                            // 前触れの灯りの明るさの真ん中（いちばん明るい色に対する割合）
constexpr float kWarnPulseRange = 0.45f;                           // 前触れの灯りが明滅する幅（同じ割合）
constexpr float kCoreSparkScale = 2.0f;                            // 光る節を撃たれたときの火花の大きさの倍率

// seed と番号から -1〜1 の数を作る
float HashSigned(unsigned int seed, int index) {
  unsigned int h = seed * 747796405u + static_cast<unsigned int>(index) * 2891336453u;
  h ^= h >> 16;
  h *= 2246822519u;
  h ^= h >> 13;
  return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f;
}

Vector4 LerpColor(const Vector4 &a, const Vector4 &b, float k) {
  return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, 1.0f};
}
} // namespace

MechSerpent::MechSerpent() = default;
MechSerpent::~MechSerpent() = default;

void MechSerpent::Initialize(Object3dRenderer *renderer, RailCamera *railCamera, Player *player,
                             InterceptCue *cue, const std::vector<Vector3> &path, float spawnT,
                             float pathSeconds, const SerpentSpec &spec) {
  path_ = path;
  railCamera_ = railCamera;
  player_ = player;
  cue_ = cue;
  pathSeconds_ = (std::max)(pathSeconds, 1e-3f);
  spec_ = spec;
  std::sort(spec_.coreSegments.begin(), spec_.coreSegments.end());
  state_ = State::Approach;
  stateTimer_ = 0.0f;
  hasAppeared_ = false;
  events_.clear();
  explodedCount_ = 0;
  flinchTimer_ = 0.0f;
  lastRailT_ = railCamera_ ? railCamera_->GetT() : spawnT;
  headSeconds_ = (std::max)(lastRailT_ - spawnT, 0.0f) / kPathTPerSecond_;

  // 道筋の長さの表: 点の番号を細かく区切り、区切りごとの初めからの長さを足していく
  arcIndex_.clear();
  arcLength_.clear();
  const int pointCount = static_cast<int>(path_.size()) - 1;
  const int samples = (std::max)(pointCount, 1) * kArcSamplesPerPoint_;
  float length = 0.0f;
  Vector3 previous = PathPoint(0.0f);
  for (int k = 0; k <= samples; ++k) {
    const float u = static_cast<float>(k) / static_cast<float>(kArcSamplesPerPoint_);
    const Vector3 p = PathPoint(u);
    length += Length(p - previous);
    previous = p;
    arcIndex_.push_back(u);
    arcLength_.push_back(length);
  }

  // 部品: 光る節の核は、体の節の外側（カメラの側）の面に突き出た箱
  partSpecs_.clear();
  for (int segment : spec_.coreSegments) {
    if (segment < 0 || segment >= kSegmentCount) {
      continue;
    }
    const float half = segment == 0 ? kHeadHalf_ : kSegmentHalf_;
    partSpecs_.push_back({segment, EnemyPart::Role::Core, {0.0f, half + kCoreHalf_ - kCoreSink_, 0.0f},
                          {kCoreHalf_, kCoreHalf_, kCoreHalf_}, kCoreDimColor});
  }
  parts_.assign(partSpecs_.size(), nullptr);
  cueIds_.assign(partSpecs_.size(), 0);
  chargeFlashTimer_ = 0.0f;
  aliveCores_ = 0;

  for (int i = 0; i < kSegmentCount; ++i) {
    const float half = i == 0 ? kHeadHalf_ : kSegmentHalf_;
    auto obj = std::make_unique<Object3d>();
    obj->Initialize(renderer);
    obj->SetModel(kBoxModelPath);
    obj->SetScale({half * 2.0f, half * 2.0f, half * 2.0f});
    obj->SetColor(kBodyColor);
    obj->SetSpecularScale(0.0f);
    segments_[i] = std::move(obj);
    segmentForwards_[i] = {0.0f, 0.0f, 1.0f};
  }

  warnLamp_.reset();
  if (spec_.warnSeconds > 0.0f) {
    // 前触れの灯り: 出る所の壁の穴の中心から、壁の外へ少し出した薄い板
    const Vector3 outward = {std::sin(spec_.warnYaw), 0.0f, std::cos(spec_.warnYaw)};
    warnLamp_ = std::make_unique<Object3d>();
    warnLamp_->Initialize(renderer);
    warnLamp_->SetModel(kBoxModelPath);
    warnLamp_->SetTranslation(spec_.warnPoint + outward * kWarnLampOut_);
    warnLamp_->SetRotation({0.0f, spec_.warnYaw, 0.0f});
    warnLamp_->SetScale({kWarnLampHalfX_ * 2.0f, kWarnLampHalfY_ * 2.0f, kWarnLampHalfZ_ * 2.0f});
    warnLamp_->SetSpecularScale(0.0f);
  }

  // 出たときにもう止まる所を過ぎていたら、止まる所から始める
  if (headSeconds_ >= spec_.stopSeconds) {
    headSeconds_ = spec_.stopSeconds;
  }
  PlaceSegments();
  BuildSegments();
}

void MechSerpent::SetPart(int index, EnemyPart *part) {
  if (index < 0 || index >= static_cast<int>(parts_.size())) {
    return;
  }
  if (!parts_[index] && part) {
    aliveCores_++;
  }
  parts_[index] = part;
  if (part) {
    part->SetBaseColor(kCoreDimColor);
  }
}

Vector3 MechSerpent::PathPoint(float u) const {
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

float MechSerpent::IndexAtLength(float length) const {
  if (arcLength_.empty() || length <= 0.0f) {
    return 0.0f;
  }
  if (length >= arcLength_.back()) {
    return arcIndex_.back();
  }
  // 長さの表から、length をはさむ 2 つの区切りを探して間を割る
  const auto it = std::upper_bound(arcLength_.begin(), arcLength_.end(), length);
  const size_t hi = static_cast<size_t>(it - arcLength_.begin());
  const size_t lo = hi - 1;
  const float span = arcLength_[hi] - arcLength_[lo];
  const float k = span > 1e-5f ? (length - arcLength_[lo]) / span : 0.0f;
  return arcIndex_[lo] + (arcIndex_[hi] - arcIndex_[lo]) * k;
}

float MechSerpent::HeadLength(float seconds) const {
  if (arcLength_.size() < 2) {
    return 0.0f;
  }
  const int pointCount = static_cast<int>(path_.size()) - 1;
  // 秒数を点の番号に直し、長さの表の区切りの間を割る
  auto lengthAtIndex = [this](float u) {
    const float k = std::clamp(u * kArcSamplesPerPoint_, 0.0f, static_cast<float>(arcLength_.size() - 1));
    const size_t lo = (std::min)(static_cast<size_t>(k), arcLength_.size() - 2);
    const float f = k - static_cast<float>(lo);
    return arcLength_[lo] + (arcLength_[lo + 1] - arcLength_[lo]) * f;
  };
  const float u = (std::max)(seconds, 0.0f) / pathSeconds_ * static_cast<float>(pointCount);
  if (u <= static_cast<float>(pointCount)) {
    return lengthAtIndex(u);
  }
  // 道筋の終わりを過ぎたら、最後の点の間の速さのまま進む
  const float pointSeconds = pathSeconds_ / static_cast<float>(pointCount);
  const float endSpeed = (arcLength_.back() - lengthAtIndex(static_cast<float>(pointCount - 1))) / pointSeconds;
  return arcLength_.back() + (seconds - pathSeconds_) * endSpeed;
}

void MechSerpent::Update() {
  stateTimer_ += kFrameTime_;
  switch (state_) {
  case State::Approach:
    UpdateApproach();
    break;
  case State::Charge:
    UpdateCharge();
    break;
  case State::Result:
    UpdateResult();
    break;
  case State::Leave:
    UpdateLeave();
    break;
  case State::Dying:
    UpdateDying();
    break;
  case State::Finished:
    return;
  }
  if (state_ == State::Finished) {
    return;
  }
  if (flinchTimer_ > 0.0f) {
    flinchTimer_ = (std::max)(flinchTimer_ - kFrameTime_, 0.0f);
    flinchClock_ += kFrameTime_;
  }
  if (!hasAppeared_ && headSeconds_ >= spec_.appearSeconds) {
    hasAppeared_ = true;
    events_.push_back(Event::Appeared);
  }
  UpdateWarn();
  BuildSegments();
  UpdateGlow();
}

void MechSerpent::UpdateApproach() {
  // レールが進んだ分だけ道筋を進む（巻き戻しはそのまま戻る）
  const float railT = railCamera_ ? railCamera_->GetT() : lastRailT_;
  headSeconds_ = (std::max)(headSeconds_ + (railT - lastRailT_) / kPathTPerSecond_, 0.0f);
  lastRailT_ = railT;
  if (headSeconds_ >= spec_.stopSeconds) {
    headSeconds_ = spec_.stopSeconds;
    PlaceSegments();
    StartCharge();
    return;
  }
  PlaceSegments();
}

void MechSerpent::StartCharge() {
  state_ = State::Charge;
  stateTimer_ = 0.0f;
  events_.push_back(Event::ChargeStarted);
  SoundManager::GetInstance()->PlaySE("boss_charge");
  // 溜め始め: 光る節が白く光り、火花が散る
  chargeFlashTimer_ = kChargeFlashSeconds_;
  for (size_t i = 0; i < parts_.size(); ++i) {
    if (!parts_[i]) {
      continue;
    }
    EffectManager::GetInstance()->PlayEffect(EffectType::HitSpark, CorePosition(static_cast<int>(i)),
                                             kElectricColor, kChargeSparkScale_);
    if (cue_) {
      cueIds_[i] = cue_->AddMark();
    }
  }
}

void MechSerpent::UpdateCharge() {
  if (stateTimer_ >= spec_.chargeSeconds) {
    FireZap();
  }
}

void MechSerpent::FireZap() {
  const Vector3 &head = segmentPositions_[0];
  if (spec_.wallZap) {
    // 予告: 頭から壁を伝って真下へ走る
    if (cue_) {
      cue_->PlayZap(head, head - kWorldUp * kWallZapLength_);
    }
  } else if (aliveCores_ > 0 && player_) {
    // 残った光る節の数だけ自機へ電撃が走る。ダメージは決まりの上限まで
    const Vector3 target = player_->GetTransform().translate;
    if (cue_) {
      for (size_t i = 0; i < parts_.size(); ++i) {
        if (parts_[i]) {
          cue_->PlayZap(CorePosition(static_cast<int>(i)), target);
        }
      }
    }
    int damage = aliveCores_ * kDamagePerLine_;
    if (spec_.zapMax > 0) {
      damage = (std::min)(damage, spec_.zapMax);
    }
    player_->TakeDamage(damage);
  }
  SoundManager::GetInstance()->PlaySE("laser_shot");
  VanishParts();
  state_ = State::Result;
  stateTimer_ = 0.0f;
}

void MechSerpent::UpdateResult() {
  if (stateTimer_ >= kResultSeconds_) {
    state_ = State::Leave;
    stateTimer_ = 0.0f;
    if (spec_.isFinal) {
      events_.push_back(Event::Ended);
    }
  }
}

void MechSerpent::UpdateLeave() {
  // 道筋の残りを速く進み、尾まで道筋の終わりの先（壁の中）に入ったら終わる
  headSeconds_ += kFrameTime_ * kLeaveRate_;
  PlaceSegments();
  const float tailLength = HeadLength(headSeconds_) - static_cast<float>(kSegmentCount - 1) * kSegmentSpacing_;
  if (arcLength_.empty() || tailLength >= arcLength_.back()) {
    VanishParts();
    state_ = State::Finished;
  }
}

void MechSerpent::StartDying() {
  state_ = State::Dying;
  stateTimer_ = 0.0f;
  sparkTimer_ = 0.0f;
  explodedCount_ = 0;
  VanishParts();
}

void MechSerpent::UpdateDying() {
  EffectManager *effects = EffectManager::GetInstance();
  const int alive = kSegmentCount - explodedCount_;

  // 残った節のどれかに電気の火花を出す
  sparkTimer_ -= kFrameTime_;
  if (sparkTimer_ <= 0.0f && alive > 0) {
    sparkTimer_ = kDyingSparkInterval_;
    const unsigned int seed = static_cast<unsigned int>(stateTimer_ / kFrameTime_);
    const int segment = static_cast<int>((HashSigned(seed, 99) * 0.5f + 0.5f) * static_cast<float>(alive)) % alive;
    effects->PlayEffect(EffectType::HitSpark, segmentPositions_[segment], kElectricColor, kDyingSparkScale_);
  }

  // 尾から頭へ、決まった間で 1 節ずつ爆発する
  while (explodedCount_ < kSegmentCount &&
         stateTimer_ >= kDyingLead_ + kDyingStep_ * static_cast<float>(explodedCount_)) {
    const int segment = kSegmentCount - 1 - explodedCount_;
    const bool isHead = segment == 0;
    effects->PlayEffect(EffectType::EnemyDeath, segmentPositions_[segment], kExplosionColor,
                        isHead ? kHeadExplosionScale_ : kExplosionScale_);
    if (isHead) {
      effects->PlayShockwave(segmentPositions_[segment]);
      SoundManager::GetInstance()->PlaySE("boss_explosion");
    } else {
      SoundManager::GetInstance()->PlaySE("enemy_destroy");
    }
    if (railCamera_) {
      railCamera_->Shake(isHead ? kHeadExplosionShake_ : kExplosionShake_, kExplosionShakeSeconds_);
    }
    explodedCount_++;
  }

  const float endSeconds = kDyingLead_ + kDyingStep_ * static_cast<float>(kSegmentCount - 1) + kDyingTail_;
  if (explodedCount_ >= kSegmentCount && stateTimer_ >= endSeconds) {
    events_.push_back(Event::Ended);
    state_ = State::Finished;
  }
}

void MechSerpent::UpdateWarn() {
  if (!warnLamp_) {
    return;
  }
  warnClock_ += kFrameTime_;
  const float brightness = kWarnPulseBase + kWarnPulseRange * std::sin(warnClock_ * kWarnPulseSpeed_);
  warnLamp_->SetColor({kWarnColor.x * brightness, kWarnColor.y * brightness,
                       kWarnColor.z * brightness, 1.0f});

  // 出るまでの間、壁が揺れる（カメラを少しずつ揺らし続ける）
  if (headSeconds_ < spec_.warnSeconds && railCamera_ && state_ == State::Approach) {
    warnShakeTimer_ -= kFrameTime_;
    if (warnShakeTimer_ <= 0.0f) {
      warnShakeTimer_ = kWarnShakeInterval_;
      railCamera_->Shake(kWarnShakeIntensity_, kWarnShakeSeconds_);
    }
  }
}

void MechSerpent::PlaceSegments() {
  if (path_.size() < 2) {
    return;
  }
  const int pointCount = static_cast<int>(path_.size()) - 1;
  const Vector3 end = path_.back();
  const Vector3 endDir = SafeNormalize(path_.back() - path_[pointCount - 1]);
  const float endLength = arcLength_.empty() ? 0.0f : arcLength_.back();

  // 節 i は、頭から道筋の上を i × 間隔 だけ戻った所。道筋の終わりの先は、終わりの向きのまま伸ばす
  const float headLength = HeadLength(headSeconds_);
  for (int i = 0; i < kSegmentCount; ++i) {
    const float length = headLength - static_cast<float>(i) * kSegmentSpacing_;
    if (length > endLength) {
      segmentPositions_[i] = end + endDir * (length - endLength);
    } else {
      segmentPositions_[i] = PathPoint(IndexAtLength(length));
    }
  }

  // 向き: 頭は道筋の前後の点から、体の節は 1 つ前の節の方を向く
  if (headLength > endLength) {
    segmentForwards_[0] = endDir;
  } else {
    const float headIndex = IndexAtLength(headLength);
    const Vector3 headDir = PathPoint(headIndex + kHeadingStep_) - PathPoint(headIndex - kHeadingStep_);
    if (Length(headDir) > 1e-4f) {
      segmentForwards_[0] = Normalize(headDir);
    }
  }
  for (int i = 1; i < kSegmentCount; ++i) {
    const Vector3 toFront = segmentPositions_[i - 1] - segmentPositions_[i];
    segmentForwards_[i] = Length(toFront) > 1e-3f ? Normalize(toFront) : segmentForwards_[i - 1];
  }
}

void MechSerpent::BuildSegments() {
  const Vector3 eye = railCamera_ ? railCamera_->GetRailPosition() : Vector3{0.0f, 0.0f, 0.0f};
  const float flinch = flinchTimer_ / kFlinchSeconds_;
  const unsigned int shakeSeed = static_cast<unsigned int>(stateTimer_ / kFrameTime_);
  for (int i = 0; i < kSegmentCount; ++i) {
    const Vector3 &forward = segmentForwards_[i];

    // 節の外の向き: カメラの方を、前の向きに直角にしたもの
    Vector3 toEye = eye - segmentPositions_[i];
    toEye = toEye - forward * Dot(toEye, forward);
    if (Length(toEye) < 1e-3f) {
      toEye = kWorldUp - forward * Dot(kWorldUp, forward);
    }
    const Vector3 up = SafeNormalize(toEye);
    const Vector3 right = Cross(up, forward);

    // 溜めを止められた間は、頭から尾へ伝わる波で外の向きに揺れる
    const float wave = std::sin(flinchClock_ * kFlinchFrequency_ - static_cast<float>(i) * kFlinchWave_);
    Vector3 position = segmentPositions_[i] + up * (kFlinchAmplitude_ * wave * flinch);

    // 倒された間は、毎フレームばらばらの向きにがたがた揺れる
    if (state_ == State::Dying) {
      position = position + Vector3{HashSigned(shakeSeed, i * 3), HashSigned(shakeSeed, i * 3 + 1),
                                    HashSigned(shakeSeed, i * 3 + 2)} *
                                kDyingShake_;
    }

    // 行は 右・外・前・位置（行ベクトルを掛ける向き）
    Matrix4x4 &frame = segmentFrames_[i];
    frame = MakeIdentity4x4();
    frame.m[0][0] = right.x;    frame.m[0][1] = right.y;    frame.m[0][2] = right.z;
    frame.m[1][0] = up.x;       frame.m[1][1] = up.y;       frame.m[1][2] = up.z;
    frame.m[2][0] = forward.x;  frame.m[2][1] = forward.y;  frame.m[2][2] = forward.z;
    frame.m[3][0] = position.x; frame.m[3][1] = position.y; frame.m[3][2] = position.z;

    // 同じ向きのオイラー角（MakeRotateMatrix は X → Y → Z の順に回す）
    segmentRotations_[i] = {std::atan2(up.z, forward.z), std::asin(std::clamp(-right.z, -1.0f, 1.0f)),
                            std::atan2(right.y, right.x)};

    segments_[i]->SetTranslation(position);
    segments_[i]->SetRotation(segmentRotations_[i]);
  }
  UpdateTransform();
}

void MechSerpent::UpdateGlow() {
  // 溜める前は、核がゆっくり脈打つ
  if (state_ == State::Approach) {
    idleClock_ += kFrameTime_;
    const float pulse = 0.5f + 0.5f * std::sin(idleClock_ * kIdlePulseSpeed_);
    for (EnemyPart *part : parts_) {
      if (part) {
        part->SetBaseColor(LerpColor(kCoreDimColor, kCoreIdleColor, pulse));
      }
    }
    return;
  }
  if (state_ != State::Charge) {
    return;
  }
  // 光る節は頭の側から順に強まり、迎撃の合図の拍ごとに明るく膨らむ。鳴りっぱなしの間は白く焼けて膨らんだまま
  // 溜め始めの少しのあいだは白く光る
  const float charge = spec_.chargeSeconds > 0.0f ? std::clamp(stateTimer_ / spec_.chargeSeconds, 0.0f, 1.0f) : 1.0f;
  const float beat = cue_ ? cue_->GetBeat() : 0.0f;
  const bool isSteady = cue_ && cue_->IsSteady();
  chargeFlashTimer_ = (std::max)(chargeFlashTimer_ - kFrameTime_, 0.0f);
  const float flash = chargeFlashTimer_ / kChargeFlashSeconds_;
  const int count = static_cast<int>(parts_.size());
  for (int i = 0; i < count; ++i) {
    if (!parts_[i]) {
      continue;
    }
    const float start = count > 1 ? kGlowLead_ * static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
    const float glow = std::clamp((charge - start) / kGlowRamp_, 0.0f, 1.0f);
    Vector4 color = isSteady ? kCoreWhiteHotColor
                             : LerpColor(LerpColor(kCoreIdleColor, kCoreGlowColor, glow), kCoreBeatColor, beat);
    color = LerpColor(color, kCoreFlashColor, flash);
    parts_[i]->SetBaseColor(color);
    const float pulse = 1.0f + kCoreBeatScale_ * (isSteady ? 1.0f : beat);
    parts_[i]->SetPulseScale(pulse);
    if (cue_ && cueIds_[i] != 0) {
      cue_->UpdateMark(cueIds_[i], CorePosition(i), partSpecs_[i].halfSize.x * pulse, spec_.chargeSeconds - stateTimer_);
    }
  }
}

Vector3 MechSerpent::GetCoreCenter() const {
  if (partSpecs_.empty()) {
    return segmentPositions_[0];
  }
  Vector3 sum = {0.0f, 0.0f, 0.0f};
  for (int i = 0; i < static_cast<int>(partSpecs_.size()); ++i) {
    sum = sum + CorePosition(i);
  }
  return sum * (1.0f / static_cast<float>(partSpecs_.size()));
}

Vector3 MechSerpent::CorePosition(int index) const {
  const PartSpec &spec = partSpecs_[index];
  return TransformPoint(spec.localPosition, segmentFrames_[spec.segment]);
}

void MechSerpent::UpdateTransform() {
  for (auto &segment : segments_) {
    if (segment) {
      segment->Update();
    }
  }
  if (warnLamp_) {
    warnLamp_->Update();
  }
}

void MechSerpent::Draw() {
  if (state_ == State::Finished) {
    return;
  }
  // 倒された後は、爆発し終えた尾の側の節を描かない
  const int visible = kSegmentCount - explodedCount_;
  for (int i = 0; i < visible; ++i) {
    segments_[i]->Draw();
  }
  // 前触れの灯りは、出るまでの間と出てから少しのあいだ点く
  if (warnLamp_ && headSeconds_ < spec_.warnSeconds + kWarnLinger_) {
    warnLamp_->Draw();
  }
}

std::vector<MechSerpent::Event> MechSerpent::TakeEvents() {
  std::vector<Event> events;
  events.swap(events_);
  return events;
}

const Matrix4x4 &MechSerpent::GetPartFrame(int index) const {
  if (index < 0 || index >= static_cast<int>(partSpecs_.size())) {
    return segmentFrames_[0];
  }
  return segmentFrames_[partSpecs_[index].segment];
}

Vector3 MechSerpent::GetPartRotation(int index) const {
  if (index < 0 || index >= static_cast<int>(partSpecs_.size())) {
    return segmentRotations_[0];
  }
  return segmentRotations_[partSpecs_[index].segment];
}

bool MechSerpent::IsPartActive(int index) const {
  (void)index;
  return state_ == State::Charge;
}

void MechSerpent::OnPartDestroyed(int index) {
  if (index < 0 || index >= static_cast<int>(parts_.size()) || !parts_[index]) {
    return;
  }
  parts_[index] = nullptr;
  aliveCores_--;
  if (cue_ && cueIds_[index] != 0) {
    cue_->BreakMark(cueIds_[index]);
    cueIds_[index] = 0;
  }
  EffectManager::GetInstance()->PlayEffect(EffectType::HitSpark,
                                           CorePosition(index),
                                           kSparkColor, kCoreSparkScale);

  // 光る節を全部撃たれたら溜めが止まる。最後の見せ場の出番はその場で倒れる
  if (aliveCores_ <= 0 && state_ == State::Charge) {
    if (spec_.isFinal) {
      StartDying();
    } else {
      flinchTimer_ = kFlinchSeconds_;
      SoundManager::GetInstance()->PlaySE("boss_counter");
      state_ = State::Result;
      stateTimer_ = 0.0f;
    }
  }
}

void MechSerpent::VanishParts() {
  for (size_t i = 0; i < parts_.size(); ++i) {
    if (parts_[i]) {
      parts_[i]->Vanish();
      parts_[i] = nullptr;
    }
    if (cue_ && cueIds_[i] != 0) {
      cue_->RemoveMark(cueIds_[i]);
      cueIds_[i] = 0;
    }
  }
  aliveCores_ = 0;
}
