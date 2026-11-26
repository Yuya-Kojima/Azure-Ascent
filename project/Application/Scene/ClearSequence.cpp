#include "ClearSequence.h"
#include "Camera/GameCamera.h"
#include "Camera/RailCamera.h"
#include "Core/WindowSystem.h"
#include "Effect/EffectManager.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Sprite/Sprite.h"
#include "Scene/CaptionCard.h"
#include "Scene/ClearFlight.h"
#include "../../externals/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <utility>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 調整値の保存先
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/ClearSequenceConfig.json";

// 光の玉と柱のモデル（玉は半径 1 の球、柱は半径 1・高さ 0〜1 の円柱の側面）
const char *const kOrbModelPath = "resources/models/clear_light/light_orb.obj";
const char *const kPillarModelPath =
    "resources/models/clear_light/light_pillar.obj";

// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
// 演出のカメラの遠くの描画の限界（柱の天辺まで映す）
const float kCameraFarClip = 2000.0f;
// 遠くのカメラの見る点を、カメラから置く距離
const float kLookDistance = 10.0f;
// 晴れの円が広がりきったときの半径（度）。太陽から見て空の反対側の地平線より外まで広げ、境目を空から追い出す
const float kSpreadEndDeg = 150.0f;
// 揺れに使う 3 つの sin の 1 秒あたりの角度（ラジアン）と、ずらし
const Vector3 kShakeFrequency = {53.0f, 71.0f, 37.0f};
const Vector3 kShakePhase = {0.0f, 1.3f, 0.7f};
// 小爆発の数の上限（間隔の調整値が小さすぎても表が大きくなりすぎないように）
const size_t kMaxExplosions = 200;
// 小爆発の間隔の下限の下限（秒）
const float kMinExplosionInterval = 0.02f;
// 爆発のパーティクルの大きさの倍率を決めたときのボスの大きさ（ボスの大きさに比例して大きくする）
const float kEffectBossSize = 10.0f;
// 大爆発で、真ん中の爆発と一緒に爆発させる体の表面の点の数（表の先頭から）
const size_t kBlastPointCount = 4;
// 向きの崩れの回転の軸が決まらないとみなす長さ
const float kTiltAxisEpsilon = 0.0001f;
// 体の表面の点を下へ押す向き（爆発した所が下がるように崩す）
const Vector3 kTiltPush = {0.0f, -1.0f, 0.0f};
// 光の粒が飛ぶ向きの上下を縮める割合（上下の黒い帯の内側に収める）
const float kMoteVerticalSquash = 0.5f;
// 戻ってくる光の粒が、真ん中に着く直前に小さくなる割合
const float kMoteArriveShrink = 0.5f;
// 時定数の下限（秒。0 で割らないように）
const float kMinTimeConstant = 0.01f;
// 黄金角（ラジアン）。光の粒の向きを球の上に偏りなく並べるのに使う
const float kGoldenAngle = 2.39996323f;
// 柱の太さが脈打つ 1 秒あたりの角度（ラジアン）
const float kPillarPulseFrequency = 20.0f;
// 溜めの間に玉が脈打つ 1 秒あたりの角度（ラジアン）と大きさの割合
const float kOrbPulseFrequency = 14.0f;
const float kOrbPulseRatio = 0.08f;
// UV は画像全体をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

float ToRadians(float degrees) {
  return degrees * std::numbers::pi_v<float> / 180.0f;
}

/// <summary>
/// 3 つの周波数の sin でずらした揺れ（大きさ 1）
/// </summary>
Vector3 Wobble(float time, const Vector3 &frequency, const Vector3 &phase) {
  return {std::sin(time * frequency.x + phase.x),
          std::sin(time * frequency.y + phase.y),
          std::sin(time * frequency.z + phase.z)};
}

/// <summary>
/// 「STAGE CLEAR」のカード（横線の上下に文字を置き、光で現れる出し方）。時刻と置き場所は ClearSequence が調整値で書き換える
/// </summary>
CaptionCard::Settings MakeClearCardSettings() {
  CaptionCard::Settings settings;
  settings.upperPath = "resources/UI/Clear/clear_title.png";
  settings.lowerPath = "resources/UI/Clear/clear_result.png";
  settings.revealByLight = true;
  settings.sparkPath = "resources/UI/GameOver/route_dot.png";
  return settings;
}

Vector4 ToColor(const Vector3 &color, float intensity) {
  return {color.x * intensity, color.y * intensity, color.z * intensity, 1.0f};
}

/// <summary>
/// index と salt から決まる 0〜1 の値（同じ組なら毎回同じ値。乱数の代わりに使い、時刻を戻しても同じ絵にする）
/// </summary>
float Hash01(uint32_t index, uint32_t salt) {
  uint32_t x = index * 0x9E3779B9u + salt * 0x85EBCA6Bu + 0x27D4EB2Fu;
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  const uint32_t kMask = 0xFFFFFFu;
  return float(x & kMask) / float(kMask);
}

/// <summary>
/// 光のモデルを作る（ライティングなしで、中心が白く光る描き方）
/// </summary>
std::unique_ptr<Object3d> MakeLightObject(Object3dRenderer *renderer,
                                          const char *modelPath) {
  auto object = std::make_unique<Object3d>();
  object->Initialize(renderer);
  object->SetModel(modelPath);
  object->SetEnableLighting(false);
  object->SetFlatUnlit(false);
  return object;
}
} // namespace

ClearSequence::ClearSequence() : camera_(std::make_unique<GameCamera>()) {}

ClearSequence::~ClearSequence() = default;

void ClearSequence::Initialize(SpriteRenderer *spriteRenderer,
                               Object3dRenderer *object3dRenderer) {
  orb_ = MakeLightObject(object3dRenderer, kOrbModelPath);
  for (std::unique_ptr<Object3d> &mote : motes_) {
    mote = MakeLightObject(object3dRenderer, kOrbModelPath);
  }
  pillar_ = MakeLightObject(object3dRenderer, kPillarModelPath);
  tipOrb_ = MakeLightObject(object3dRenderer, kOrbModelPath);

  // 上下の黒い帯：白い 1 画素の画像を黒く塗り、画面の幅いっぱいに伸ばす
  // 上の帯は下の辺、下の帯は上の辺を基準にして、画面の外から出し入れする
  auto makeBar = [spriteRenderer](const Vector2 &anchor) {
    auto bar = std::make_unique<Sprite>();
    bar->Initialize(spriteRenderer, "resources/white1x1.png");
    bar->SetAnchorPoint(anchor);
    bar->SetColor({0.0f, 0.0f, 0.0f, 1.0f});
    return bar;
  };
  letterboxTop_ = makeBar({0.5f, 1.0f});
  letterboxBottom_ = makeBar({0.5f, 0.0f});

  card_ = std::make_unique<CaptionCard>();
  card_->Initialize(spriteRenderer, MakeClearCardSettings());
  flight_ = std::make_unique<ClearFlight>();

  camera_->SetFarClip(kCameraFarClip);

  LoadConfig();
}

void ClearSequence::VisitConfigValues(
    const std::function<void(const std::string &key, float &value)> &visit) {
  // 3 成分の値は「キー.x」「キー.y」「キー.z」で 1 つずつ渡す
  auto visitVector = [&](const std::string &key, Vector3 &value) {
    visit(key + ".x", value.x);
    visit(key + ".y", value.y);
    visit(key + ".z", value.z);
  };

  // 時間
  visit("hitSec", hitSec_);
  visit("hitStopSec", hitStopSec_);
  visit("slowScale", slowScale_);
  visit("slowRecoverSec", slowRecoverSec_);
  visit("approachSec", approachSec_);
  visit("breakSec", breakSec_);
  visit("blastSec", blastSec_);
  visit("gatherSec", gatherSec_);
  visit("pillarSec", pillarSec_);
  visit("convergeSec", convergeSec_);
  visit("holdSec", holdSec_);
  visit("clearSec", clearSec_);
  visit("afterglowSec", afterglowSec_);
  visit("skipLockSec", skipLockSec_);
  visit("hudFadeSec", hudFadeSec_);
  visit("letterbox.aspect", letterboxAspect_);
  visit("letterbox.inSec", letterboxInSec_);
  visit("letterbox.outSec", letterboxOutSec_);
  // 晴れ
  visit("spreadStartDeg", spreadStartDeg_);
  visit("sunBoost", sunBoost_);
  visit("sunBoostFadeSec", sunBoostFadeSec_);
  visit("burstSec", burstSec_);
  visit("frameOutDeg", frameOutDeg_);
  visit("finishSec", finishSec_);
  // カメラ
  visitVector("camera.nearOffset", nearCameraOffset_);
  visitVector("camera.nearLookOffset", nearLookOffset_);
  visit("camera.nearFov", nearFov_);
  visit("camera.approachLookRatio", approachLookRatio_);
  visit("camera.pushIn", pushIn_);
  visit("camera.breakShake", breakShake_);
  visit("camera.blastShake", blastShake_);
  visit("camera.blastShakeSec", blastShakeSec_);
  visitVector("camera.farOffset", farCameraOffset_);
  visit("camera.farPitchDeg", farPitchDeg_);
  visit("camera.farYawDeg", farYawDeg_);
  visit("camera.farFov", farFov_);
  visit("camera.launchShake", launchShake_);
  visit("camera.launchShakeSec", launchShakeSec_);
  // ボス：小爆発
  visit("explosion.firstDelay", explosionFirstDelay_);
  visit("explosion.interval", explosionInterval_);
  visit("explosion.intervalRatio", explosionIntervalRatio_);
  visit("explosion.minInterval", explosionMinInterval_);
  visit("explosion.endGap", explosionEndGap_);
  visit("explosion.scaleStart", explosionScaleStart_);
  visit("explosion.scaleEnd", explosionScaleEnd_);
  visitVector("explosion.color", explosionColor_);
  // ボス：向きの崩れ
  visit("tilt.kickDeg", tiltKickDeg_);
  visit("tilt.jitter", tiltJitter_);
  visit("tilt.riseSec", tiltRiseSec_);
  visit("tilt.recoverSec", tiltRecoverSec_);
  visit("tilt.keep", tiltKeep_);
  visit("tilt.maxDeg", tiltMaxDeg_);
  // ボス：赤い点滅
  visitVector("flash.color", flashColor_);
  visit("flash.peak", flashPeak_);
  visit("flash.sec", flashSec_);
  visit("flash.floor", flashFloor_);
  // ボス：大爆発と破片
  visit("blast.scale", blastScale_);
  visit("blast.pointScale", blastPointScale_);
  visit("debris.speed", debrisSpeed_);
  visit("debris.gravity", debrisGravity_);
  visit("debris.drag", debrisDrag_);
  visit("debris.spin", debrisSpin_);
  visitVector("debris.rimColor", debrisRimColor_);
  visit("debris.rimIntensity", debrisRimIntensity_);
  visit("debris.rimPower", debrisRimPower_);
  // 光の粒
  visit("mote.distance", moteDistance_);
  visit("mote.distanceJitter", moteDistanceJitter_);
  visit("mote.outSec", moteOutSec_);
  visit("mote.returnSec", moteReturnSec_);
  visit("mote.scale", moteScale_);
  visit("mote.intensity", moteIntensity_);
  // 光の玉と柱
  visitVector("light.color", glowColor_);
  visit("light.orbStartScale", orbStartScale_);
  visit("light.orbMaxScale", orbMaxScale_);
  visit("light.orbIntensity", orbIntensity_);
  visitVector("light.orbRimColor", orbRimColor_);
  visit("light.orbRimIntensity", orbRimIntensity_);
  visit("light.orbRimPower", orbRimPower_);
  visit("light.squeezeSec", squeezeSec_);
  visit("light.squeezeScale", squeezeScale_);
  visit("light.orbShrinkSec", orbShrinkSec_);
  visit("light.pillarRadius", pillarRadius_);
  visit("light.pillarHeight", pillarHeight_);
  visit("light.pillarIntensity", pillarIntensity_);
  visit("light.pillarPulse", pillarPulse_);
  visit("light.tipOrbScale", tipOrbScale_);
  visit("light.tipOrbStartRatio", tipOrbStartRatio_);
  visit("light.tipShrinkScale", tipShrinkScale_);
  visit("light.burstOrbScale", burstOrbScale_);
  visit("light.burstOrbSec", burstOrbSec_);
  visit("light.tipOrbIntensity", tipOrbIntensity_);
  // ドラゴン
  visit("dragon.enterSec", dragonEnterSec_);
  visit("dragon.backRimScale", dragonBackRimScale_);
  flight_->VisitConfigValues([&](const std::string &key, float &value) {
    visit("flight." + key, value);
  });
  // 文字のカード
  visit("card.growSec", cardGrowSec_);
  visit("card.lowerDelay", cardLowerDelay_);
  visit("card.outSec", cardOutSec_);
  visit("card.lineY", cardLineY_);
  visit("card.lineWidth", cardLineWidth_);
}

void ClearSequence::LoadConfig() {
  std::ifstream file(kConfigPath);
  if (!file.is_open()) {
    return;
  }
  nlohmann::json root;
  try {
    file >> root;
  } catch (const nlohmann::json::exception &) {
    return;
  }
  // ファイルにないキーは今の値のまま
  VisitConfigValues([&](const std::string &key, float &value) {
    if (root.contains(key) && root[key].is_number()) {
      value = root[key].get<float>();
    }
  });
}

void ClearSequence::SaveConfig() {
  nlohmann::json root;
  VisitConfigValues(
      [&](const std::string &key, float &value) { root[key] = value; });
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void ClearSequence::Start(const RailCamera &railCamera, const Vector3 &bossPos,
                          float bossSize, const Vector3 &bossRotation,
                          const std::vector<Vector3> &blastPoints) {
  isActive_ = true;
  timer_ = 0.0f;
  cues_.clear();
  bossPos_ = bossPos;
  bossSize_ = (std::max)(bossSize, 0.01f);
  bossBaseOrientation_ = MakeRotationFromMatrix(MakeRotateMatrix(bossRotation));
  blastPoints_ = blastPoints;

  // レールカメラの位置・画角と、見ている点
  // ビュー行列の逆行列（カメラのワールド行列）の3行目がカメラの正面方向、4行目が位置
  const float kRailLookDistance = 10.0f;
  Matrix4x4 railWorld = Inverse(railCamera.GetViewMatrix());
  Vector3 railLookDir = {railWorld.m[2][0], railWorld.m[2][1],
                         railWorld.m[2][2]};
  railShot_.position = {railWorld.m[3][0], railWorld.m[3][1],
                        railWorld.m[3][2]};
  railShot_.look =
      railShot_.position + Normalize(railLookDir) * kRailLookDistance;
  railShot_.fov = railCamera.GetFov();

  // 前：レールカメラからボスへの水平な向き（重なっているときはレールの前）
  Vector3 toBoss = bossPos - railShot_.position;
  toBoss.y = 0.0f;
  if (Length(toBoss) < 0.01f) {
    const Vector3 railForward = railCamera.GetRailForward();
    toBoss = {railForward.x, 0.0f, railForward.z};
  }
  forward_ = Normalize(toBoss);
  right_ = Normalize(Cross(kWorldUp, forward_));

  dragonPos_ = bossPos_;
  dragonRot_ = {};
  dragonOrientation_ = {0.0f, 0.0f, 0.0f, 1.0f};
  isFlightReset_ = false;
  UpdateDragon();
  UpdateCamera();
  UpdateLights();
  UpdateLetterbox();
}

void ClearSequence::Stop() {
  isActive_ = false;
  isPaused_ = false;
  cues_.clear();
  isOrbVisible_ = false;
  isMoteVisible_ = false;
  isPillarVisible_ = false;
  isTipOrbVisible_ = false;
  card_->Stop();
}

void ClearSequence::Update(bool isSkipPressed) {
  if (!isActive_) {
    return;
  }

  // 止めている間は、つまみで決めた時刻の姿を映し、出来事は積まない
  if (!isPaused_) {
    const float previous = timer_;
    const bool canSkip = CanSkip();
    timer_ += kFrameSec;
    // 飛ばすと最後の構図（リザルトを出す時刻）へ進む。通った出来事はまとめて積む
    if (isSkipPressed && canSkip) {
      timer_ = (std::max)(timer_, ResultTime());
    }
    PushCuesBetween(previous, timer_);
    PlayExplosionsBetween(previous, timer_);
  }

  UpdateDragon();
  UpdateCamera();
  UpdateCard();
  UpdateLetterbox();
  UpdateLights();
}

void ClearSequence::PushCuesBetween(float from, float to) {
  auto push = [&](float time, Cue cue) {
    if (from < time && time <= to) {
      cues_.push_back(cue);
    }
  };
  push(BlastStart(), Cue::BossBlast);
  push(BurstStart(), Cue::Burst);
  push(ResultTime(), Cue::Result);
}

std::vector<ClearSequence::Cue> ClearSequence::TakeCues() {
  return std::exchange(cues_, {});
}

float ClearSequence::GetTimeScale() const {
  if (!isActive_) {
    return 1.0f;
  }
  if (timer_ < hitStopSec_) {
    return 0.0f;
  }
  // hitSec_ の終わりの slowRecoverSec_ の間に、スローから普通の速さへ戻す
  const float recoverStart = hitSec_ - slowRecoverSec_;
  return Lerp(slowScale_, 1.0f, Progress(timer_, recoverStart, slowRecoverSec_));
}

float ClearSequence::GetHudAlpha() const {
  if (!isActive_) {
    return 1.0f;
  }
  return 1.0f - Progress(timer_, 0.0f, hudFadeSec_);
}

const ICamera *ClearSequence::GetCamera() const { return camera_.get(); }

Vector3 ClearSequence::ToWorld(const Vector3 &base,
                               const Vector3 &offset) const {
  return base + ToWorldDirection(offset);
}

Vector3 ClearSequence::ToWorldDirection(const Vector3 &offset) const {
  return right_ * offset.x + kWorldUp * offset.y + forward_ * offset.z;
}

//===========================
// 晴れ
//===========================

float ClearSequence::SpreadRadiusDegAt(float time) const {
  const float burstStart = BurstStart();
  if (time < burstStart) {
    return 0.0f;
  }
  // 爆発：出始めが速く、画面の端で遅くなる
  const float finishStart = burstStart + burstSec_;
  if (time < finishStart) {
    return Lerp(spreadStartDeg_, frameOutDeg_,
                EaseOutQuad(Progress(time, burstStart, burstSec_)));
  }
  // 抜ける：画面の外を、だんだん速く空の反対側まで
  return Lerp(frameOutDeg_, kSpreadEndDeg,
              EaseInQuad(Progress(time, finishStart, finishSec_)));
}

float ClearSequence::SpreadAt(float time) const {
  if (time < BurstStart()) {
    return 0.0f;
  }
  return std::clamp((SpreadRadiusDegAt(time) - spreadStartDeg_) /
                        (std::max)(frameOutDeg_ - spreadStartDeg_, 0.001f),
                    0.0f, 1.0f);
}

float ClearSequence::SunBoostAt(float time) const {
  if (time < BurstStart()) {
    return 1.0f;
  }
  return Lerp(sunBoost_, 1.0f,
              EaseOutQuad(Progress(time, BurstStart(), sunBoostFadeSec_)));
}

float ClearSequence::EdgeDarkAt(float time) const {
  return 1.0f - Progress(time, BurstStart() + burstSec_, finishSec_);
}

float ClearSequence::ConvergeAt(float time) const {
  // 始めはゆっくり、終わりほど速く細くなる
  return EaseInQuad(Progress(time, ConvergeStart(), convergeSec_));
}

StageLighting::ClearState ClearSequence::GetClearState() const {
  StageLighting::ClearState state;
  if (!isActive_) {
    return state;
  }
  // 晴れの円の中心と晴れた空の太陽：遠くのカメラから柱の天辺が見える向き
  state.center = SunDirection();
  state.sunDirection = state.center;
  state.hasSunDirection = true;
  // 爆発した瞬間から、円が spreadStartDeg_ の大きさで現れて広がる
  if (timer_ >= BurstStart()) {
    state.radius = ToRadians(SpreadRadiusDegAt(timer_));
    state.amount = SpreadAt(timer_);
    // 押しやられた闇は、境目が画面を抜けてから薄れて消える
    state.edgeDarkScale = EdgeDarkAt(timer_);
    state.sunIntensityScale = SunBoostAt(timer_);
  }
  return state;
}

//===========================
// ボス
//===========================

std::vector<float> ClearSequence::ExplosionTimes() const {
  // 寄り始めの少しあとから、間隔を縮めながら、大爆発の explosionEndGap_ 前まで並べる
  std::vector<float> times;
  const float minInterval = (std::max)(explosionMinInterval_, kMinExplosionInterval);
  const float last = BlastStart() - explosionEndGap_;
  float interval = (std::max)(explosionInterval_, minInterval);
  float time = BreakStart() + explosionFirstDelay_;
  while (time <= last && times.size() < kMaxExplosions) {
    times.push_back(time);
    time += interval;
    interval = (std::max)(interval * explosionIntervalRatio_, minInterval);
  }
  return times;
}

const Vector3 &ClearSequence::ExplosionPoint(size_t index) const {
  static const Vector3 kCenter = {0.0f, 0.0f, 0.0f};
  if (blastPoints_.empty()) {
    return kCenter;
  }
  return blastPoints_[index % blastPoints_.size()];
}

Vector3 ClearSequence::TiltAxis(size_t index) const {
  // 爆発した点を下へ押したときに体が回る軸（点の位置と押す向きの外積）
  const Vector3 axis = Cross(ExplosionPoint(index), kTiltPush);
  if (Length(axis) < kTiltAxisEpsilon) {
    return {1.0f, 0.0f, 0.0f};
  }
  return Normalize(axis);
}

float ClearSequence::TiltAngle(size_t index) const {
  const float jitter =
      tiltJitter_ * (2.0f * Hash01(static_cast<uint32_t>(index), 1u) - 1.0f);
  return ToRadians(tiltKickDeg_) * (1.0f + jitter);
}

Quaternion ClearSequence::BossOrientationAt(float time) const {
  // 爆発ごとに、ガクッと崩れて（tiltRiseSec_）、tiltKeep_ の割合を残して戻る崩れを足し合わせる
  // 崩れはモデルの座標の回転（軸の向き × 角度）で足し、Start の向きのあとに掛ける
  Vector3 tilt = {0.0f, 0.0f, 0.0f};
  const std::vector<float> times = ExplosionTimes();
  for (size_t index = 0; index < times.size(); ++index) {
    const float elapsed = time - times[index];
    if (elapsed < 0.0f) {
      break;
    }
    const float rise = EaseOutQuad(Progress(elapsed, 0.0f, tiltRiseSec_));
    const float settle =
        tiltKeep_ + (1.0f - tiltKeep_) *
                        std::exp(-elapsed / (std::max)(tiltRecoverSec_,
                                                      kMinTimeConstant));
    tilt += TiltAxis(index) * (TiltAngle(index) * rise * settle);
  }
  const float length = Length(tilt);
  if (length < kTiltAxisEpsilon) {
    return bossBaseOrientation_;
  }
  const float angle = (std::min)(length, ToRadians(tiltMaxDeg_));
  return Multiply(bossBaseOrientation_,
                  MakeRotateAxisAngleQuaternion(tilt * (1.0f / length), angle));
}

Vector3 ClearSequence::BossPointToWorld(const Vector3 &point,
                                        float time) const {
  return bossPos_ + RotateVector(BossOrientationAt(time), point * bossSize_);
}

float ClearSequence::FlashAt(float time) const {
  if (!isActive_ || time < BreakStart() || time >= BlastStart()) {
    return 0.0f;
  }
  // 爆発の瞬間に flashPeak_ まで光り、flashSec_ の時定数で消える。重なったら強い方を使う
  float peak = 0.0f;
  for (float explosionTime : ExplosionTimes()) {
    const float elapsed = time - explosionTime;
    if (elapsed < 0.0f) {
      break;
    }
    peak = (std::max)(
        peak, flashPeak_ * std::exp(-elapsed / (std::max)(flashSec_,
                                                          kMinTimeConstant)));
  }
  // 壊れていくにつれて、消えきらずに赤いまま残る分が増える
  const float floor =
      flashFloor_ * EaseInQuad(Progress(time, BreakStart(), breakSec_));
  return std::clamp((std::max)(peak, floor), 0.0f, 1.0f);
}

Vector4 ClearSequence::ToFlashColor() const { return ToColor(flashColor_, 1.0f); }

void ClearSequence::PlayExplosionsBetween(float from, float to) {
  // 飛ばして柱より後へ進んだときは出さない（もう映らない）
  if (to >= PillarStart()) {
    return;
  }
  EffectManager *effects = EffectManager::GetInstance();
  const Vector4 color = ToColor(explosionColor_, 1.0f);
  const float sizeScale = bossSize_ / kEffectBossSize;

  // 小爆発：後の爆発ほど大きく
  const std::vector<float> times = ExplosionTimes();
  for (size_t index = 0; index < times.size(); ++index) {
    const float time = times[index];
    if (time <= from || to < time) {
      continue;
    }
    const float order =
        times.size() > 1 ? float(index) / float(times.size() - 1) : 1.0f;
    const float scale =
        Lerp(explosionScaleStart_, explosionScaleEnd_, order) * sizeScale;
    effects->PlayEffect(EffectType::EnemyDeathSimple,
                        BossPointToWorld(ExplosionPoint(index), time), color,
                        scale);
  }

  // 大爆発：真ん中の大きな爆発と、体の表面の何か所かの爆発を同時に
  const float blastTime = BlastStart();
  if (from < blastTime && blastTime <= to) {
    effects->PlayEffect(EffectType::EnemyDeath, bossPos_, color,
                        blastScale_ * sizeScale);
    const size_t pointCount = (std::min)(kBlastPointCount, blastPoints_.size());
    for (size_t index = 0; index < pointCount; ++index) {
      effects->PlayEffect(EffectType::EnemyDeathSimple,
                          BossPointToWorld(blastPoints_[index], blastTime),
                          color, blastPointScale_ * sizeScale);
    }
  }
}

FractureDebris::Params ClearSequence::GetDebrisParams() const {
  // 速さ・重力はボスの大きさに比例させる。寿命は柱へカットする瞬間まで（カットで隠れて消える）
  FractureDebris::Params params;
  params.explosionSpeed = debrisSpeed_ * bossSize_;
  params.gravity = {0.0f, -debrisGravity_ * bossSize_, 0.0f};
  params.maxAngularSpeed = debrisSpin_;
  params.lifetime = (std::max)(PillarStart() - BlastStart(), kFrameSec);
  params.drag = debrisDrag_;
  return params;
}

FractureDebris::Look ClearSequence::GetDebrisLook(
    const Vector4 &bossColor) const {
  FractureDebris::Look look;
  look.color = bossColor;
  look.rimColor = debrisRimColor_;
  look.rimIntensity = debrisRimIntensity_;
  look.rimPower = debrisRimPower_;
  return look;
}

//===========================
// 光の粒
//===========================

Vector3 ClearSequence::MoteDirection(size_t index) const {
  // 球の上に偏りなく並べた向き（上下は縮めて横へ広げる）を、水平な右・上・前の向きで置く
  const float count = float(kMoteCount_);
  const float y = 1.0f - 2.0f * (float(index) + 0.5f) / count;
  const float ring = std::sqrt((std::max)(1.0f - y * y, 0.0f));
  const float angle = float(index) * kGoldenAngle;
  const Vector3 local = {std::cos(angle) * ring, y * kMoteVerticalSquash,
                         std::sin(angle) * ring};
  return Normalize(ToWorldDirection(local));
}

float ClearSequence::MoteDistance(size_t index) const {
  return moteDistance_ *
         (1.0f - moteDistanceJitter_ * Hash01(static_cast<uint32_t>(index), 2u));
}

float ClearSequence::MoteDelayRatio(size_t index) const {
  return Hash01(static_cast<uint32_t>(index), 3u);
}

float ClearSequence::MoteReturnAt(size_t index, float time) const {
  // 戻り始めは粒ごとにずらし、どの粒も玉が縮む（squeezeSec_）前に着くようにする
  const float window =
      (std::max)(gatherSec_ - squeezeSec_ - moteReturnSec_, 0.0f);
  const float start = GatherStart() + MoteDelayRatio(index) * window;
  // 吸い寄せられるように、終わりほど速く
  return EaseInCubic(Progress(time, start, moteReturnSec_));
}

float ClearSequence::MoteRadiusAt(size_t index, float time) const {
  if (time < BlastStart()) {
    return 0.0f;
  }
  // 大爆発で速く飛び出して止まり、戻る分だけ真ん中へ近づく
  const float out = EaseOutCubic(Progress(time, BlastStart(), moteOutSec_));
  return MoteDistance(index) * out * (1.0f - MoteReturnAt(index, time));
}

float ClearSequence::GatheredAt(float time) const {
  float sum = 0.0f;
  for (size_t index = 0; index < kMoteCount_; ++index) {
    sum += MoteReturnAt(index, time);
  }
  return sum / float(kMoteCount_);
}

//===========================
// カメラ
//===========================

float ClearSequence::PillarLengthAt(float time) const {
  // 柱は伸び始めが一番速く、天辺へ近づくにつれて緩めて止まる
  return pillarHeight_ *
         EaseOutCubic(Progress(time, PillarStart(), pillarSec_));
}

Vector3 ClearSequence::SunDirection() const {
  const Vector3 top = bossPos_ + kWorldUp * pillarHeight_;
  return Normalize(top - FarShot().position);
}

ClearSequence::Shot ClearSequence::NearShotAt(float time) const {
  // 壊れていく・大爆発・集まるの間に、ボスへゆっくり寄っていく
  const float push = Lerp(
      1.0f, pushIn_,
      EaseInOutQuad(Progress(time, BreakStart(),
                             breakSec_ + blastSec_ + gatherSec_)));
  Shot shot;
  shot.position = ToWorld(bossPos_, nearCameraOffset_ * (bossSize_ * push));
  shot.look = ToWorld(bossPos_, nearLookOffset_ * bossSize_);
  shot.fov = nearFov_;
  return shot;
}

ClearSequence::Shot ClearSequence::FarShot() const {
  // ボスの手前の遠く低い所から、ボスの向き（farYawDeg_ だけ横へずらす）を farPitchDeg_ だけ見上げる
  Shot shot;
  shot.position = ToWorld(bossPos_, farCameraOffset_ * bossSize_);
  const Vector3 toBoss = bossPos_ - shot.position;
  const float yaw =
      std::atan2(toBoss.x, toBoss.z) + ToRadians(farYawDeg_);
  const float pitch = ToRadians(farPitchDeg_);
  const Vector3 direction = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                             std::cos(yaw) * std::cos(pitch)};
  shot.look = shot.position + direction * kLookDistance;
  shot.fov = farFov_;
  return shot;
}

ClearSequence::Shot ClearSequence::ShotAt(float time) const {
  //===========================
  // 寄る：レールカメラから、ボスの斜め前へ（見る点は先にボスへ向けきる）
  //===========================
  Shot shot = railShot_;
  if (time >= ApproachStart()) {
    const Shot nearShot = NearShotAt(time);
    const float moveEase =
        EaseInOutCubic(Progress(time, ApproachStart(), approachSec_));
    const float lookEase = EaseOutCubic(
        Progress(time, ApproachStart(), approachSec_ * approachLookRatio_));
    shot.position = Lerp(railShot_.position, nearShot.position, moveEase);
    shot.look = Lerp(railShot_.look, nearShot.look, lookEase);
    shot.fov = Lerp(railShot_.fov, nearShot.fov, moveEase);
  }

  //===========================
  // 柱を撃つ瞬間に、遠く低い所へ切り替え、そのあとは位置を最後まで動かさない
  //===========================
  if (time >= PillarStart()) {
    shot = FarShot();
  }

  //===========================
  // ドラゴンが飛び始めたら、位置はそのまま、向きだけドラゴンへ遅れて向ける
  //===========================
  if (time >= DragonEnterTime() && isFlightReset_) {
    shot.look = shot.position + flight_->GetLookDirection() * kLookDistance;
  }
  return shot;
}

Vector3 ClearSequence::ShakeAt(float time) const {
  Vector3 shake{};
  const Vector3 wobble = Wobble(time, kShakeFrequency, kShakePhase);
  // 壊れていく間に強める
  if (time >= BreakStart() && time < BlastStart()) {
    shake += wobble * (breakShake_ * bossSize_ *
                       EaseInQuad(Progress(time, BreakStart(), breakSec_)));
  }
  // 大爆発の揺れ：残り時間の2乗で弱める（柱へカットするまで）
  if (time >= BlastStart() && time < PillarStart()) {
    const float remain = 1.0f - Progress(time, BlastStart(), blastShakeSec_);
    shake += wobble * (blastShake_ * bossSize_ * remain * remain);
  }
  // 柱が登り始めた瞬間の揺れ：残り時間の2乗で弱める
  if (time >= PillarStart() && time < PillarStart() + launchShakeSec_) {
    const float remain = 1.0f - Progress(time, PillarStart(), launchShakeSec_);
    shake += wobble * (launchShake_ * bossSize_ * remain * remain);
  }
  return shake;
}

void ClearSequence::UpdateCamera() {
  const Shot shot = ShotAt(timer_);
  const Vector3 position = shot.position + ShakeAt(timer_);
  camera_->SetTranslate(position);
  camera_->SetRotate(LookRotation(position, shot.look));
  camera_->SetFovY(shot.fov);
  camera_->Update();
}

//===========================
// ドラゴン
//===========================

void ClearSequence::AdvanceFlight() {
  // 時刻を戻したとき（つまみ・やり直し）は、最初から同じ間隔で進め直して同じ動きにする
  const float flightTime = timer_ - DragonEnterTime();
  const float kTimeEpsilon = 0.0001f;
  if (!isFlightReset_ || flightTime < flight_->GetTime() - kTimeEpsilon) {
    const Shot shot = FarShot();
    flight_->Reset(shot.position, right_, forward_, shot.look - shot.position,
                   SunDirection(), shot.fov);
    flight_->SetResultTime(ResultTime() - DragonEnterTime());
    isFlightReset_ = true;
  }
  while (flight_->GetTime() + kFrameSec <= flightTime + kTimeEpsilon) {
    flight_->Step(kFrameSec);
  }
}

float ClearSequence::GetDragonBackRimScale() const {
  return isActive_ ? dragonBackRimScale_ * SpreadAt(timer_) : 0.0f;
}

void ClearSequence::UpdateDragon() {
  if (!IsDragonFlying()) {
    return;
  }
  AdvanceFlight();
  dragonPos_ = flight_->GetModelPosition();
  dragonRot_ = flight_->GetRotation();
  dragonOrientation_ = flight_->GetOrientation();
  wing_ = flight_->GetWingState();
}

//===========================
// 上下の黒い帯
//===========================

void ClearSequence::UpdateLetterbox() {
  // 帯の太さ（px）：画面の縦のうち、横と縦の比を letterboxAspect_ にするのに余る分の半分
  const float width = float(WindowSystem::kClientWidth);
  const float height = float(WindowSystem::kClientHeight);
  const float visibleHeight = width / (std::max)(letterboxAspect_, 0.01f);
  const float thickness = (std::max)((height - visibleHeight) * 0.5f, 0.0f);

  // カメラがボスへ寄り始めると出始めて止まり、リザルトの時刻の letterboxOutSec_ 前から開く
  const float in = EaseOutCubic(
      Progress(timer_, ApproachStart(), (std::max)(letterboxInSec_, 0.01f)));
  const float outSec = (std::max)(letterboxOutSec_, 0.01f);
  const float out =
      EaseInOutCubic(Progress(timer_, ResultTime() - outSec, outSec));
  const float shown = isActive_ ? thickness * in * (1.0f - out) : 0.0f;

  letterboxTop_->SetSize({width, thickness});
  letterboxTop_->SetPosition({width * 0.5f, shown});
  letterboxTop_->Update(kUvTransform);
  letterboxBottom_->SetSize({width, thickness});
  letterboxBottom_->SetPosition({width * 0.5f, height - shown});
  letterboxBottom_->Update(kUvTransform);
}

//===========================
// 文字のカード
//===========================

void ClearSequence::UpdateCard() {
  if (!isActive_ || timer_ < AfterglowStart()) {
    card_->Stop();
    return;
  }
  // ドラゴンが太陽の前を過ぎた時刻に出始め、リザルトの時刻に消えきる（まだ過ぎていなければ出さない）
  const float sunPassTime = flight_->GetSunPassTime();
  CaptionCard::Timing timing;
  timing.cardInTime = sunPassTime >= 0.0f
                          ? dragonEnterSec_ + sunPassTime
                          : afterglowSec_;
  timing.lineGrowSec = (std::max)(cardGrowSec_, 0.01f);
  timing.textInDelay = cardLowerDelay_;
  timing.cardOutSec = (std::max)(cardOutSec_, 0.01f);
  timing.cardOutTime = afterglowSec_ - timing.cardOutSec;
  card_->SetTiming(timing);

  // 上下の文字は、ゲームオーバーのカードと同じ間隔で横線から離す（px）
  const float kUpperGap = 54.0f;
  const float kLowerGap = 40.0f;
  CaptionCard::Layout layout;
  layout.lineY = cardLineY_;
  layout.lineWidth = cardLineWidth_;
  layout.upperY = cardLineY_ - kUpperGap;
  layout.lowerY = cardLineY_ + kLowerGap;
  card_->SetLayout(layout);

  card_->SetTime(timer_ - AfterglowStart());
}

//===========================
// 光の玉と柱
//===========================

void ClearSequence::UpdateLights() {
  const float converge = ConvergeAt(timer_);

  // 光の粒：大爆発で外へ飛び、集まるの間に真ん中へ戻って玉に吸い込まれる
  isMoteVisible_ = isActive_ && timer_ >= BlastStart() && timer_ < PillarStart();
  if (isMoteVisible_) {
    const Vector4 moteColor = ToColor(glowColor_, moteIntensity_);
    for (size_t index = 0; index < kMoteCount_; ++index) {
      const float back = MoteReturnAt(index, timer_);
      // 戻りきった粒は玉の中に消える
      const float radius =
          back >= 1.0f ? 0.0f
                       : moteScale_ * bossSize_ *
                             (1.0f - kMoteArriveShrink * back);
      Object3d &mote = *motes_[index];
      mote.SetScale({radius, radius, radius});
      mote.SetTranslation(bossPos_ + MoteDirection(index) *
                                         (MoteRadiusAt(index, timer_) * bossSize_));
      mote.SetColor(moteColor);
    }
  }

  // 玉：大爆発で真ん中に残り、粒が戻るほど育つ。柱が登り始めたら柱の太さまで縮んで根元で光り、収束で柱と一緒に細くなって消える
  isOrbVisible_ = isActive_ && timer_ >= BlastStart() && timer_ < BurstStart();
  if (isOrbVisible_) {
    float radius = 0.0f;
    if (timer_ < PillarStart()) {
      // 粒が戻った割合だけ膨らみ、集まるの終わりの squeezeSec_ で squeezeScale_ まで縮む
      const float grow = GatheredAt(timer_);
      const float squeeze = EaseInQuad(
          Progress(timer_, PillarStart() - squeezeSec_, squeezeSec_));
      const float pulse =
          1.0f + kOrbPulseRatio * std::sin(timer_ * kOrbPulseFrequency);
      radius = Lerp(orbStartScale_, orbMaxScale_, grow) * pulse *
               Lerp(1.0f, squeezeScale_, squeeze);
    } else {
      const float shrink =
          EaseOutCubic(Progress(timer_, PillarStart(), orbShrinkSec_));
      radius = Lerp(orbMaxScale_ * squeezeScale_, pillarRadius_, shrink) *
               (1.0f - converge);
    }
    radius *= bossSize_;
    orb_->SetScale({radius, radius, radius});
    orb_->SetTranslation(bossPos_);
    orb_->SetColor(ToColor(glowColor_, orbIntensity_));
    orb_->SetRimLight(orbRimColor_, orbRimIntensity_, orbRimPower_);
  }

  // 柱：ボスの位置から真上へ伸び、収束で細くなって消える
  isPillarVisible_ =
      isActive_ && timer_ >= PillarStart() && timer_ < BurstStart();
  if (isPillarVisible_) {
    const float kMinLength = 0.01f;
    const float radius =
        pillarRadius_ * bossSize_ *
        (1.0f + pillarPulse_ * std::sin(timer_ * kPillarPulseFrequency)) *
        (1.0f - converge);
    pillar_->SetScale(
        {radius, (std::max)(PillarLengthAt(timer_), kMinLength), radius});
    pillar_->SetTranslation(bossPos_);
    pillar_->SetColor(ToColor(glowColor_, pillarIntensity_));
  }

  // 天辺の玉：柱の先が天辺に届いてから、柱が細くなった分だけ膨らむ
  // 間の間にとても小さくなり、爆発で一気に弾けて広がりながら消える
  const float burstEnd = BurstStart() + burstOrbSec_;
  isTipOrbVisible_ = isActive_ && timer_ >= ConvergeStart() && timer_ < burstEnd;
  if (isTipOrbVisible_) {
    const float gather = tipOrbScale_ * Lerp(tipOrbStartRatio_, 1.0f, converge);
    // 間：始めに速く縮み、小さいまま爆発を待つ
    const float shrink = EaseOutCubic(Progress(timer_, HoldStart(), holdSec_));
    const float held = gather * Lerp(1.0f, tipShrinkScale_, shrink);
    const float burst = Progress(timer_, BurstStart(), burstOrbSec_);
    const float radius = Lerp(held, burstOrbScale_, EaseOutCubic(burst));
    const float remain = 1.0f - burst;
    Vector4 color = ToColor(glowColor_, tipOrbIntensity_);
    color.w = remain * remain;
    tipOrb_->SetScale({radius, radius, radius});
    tipOrb_->SetTranslation(bossPos_ + kWorldUp * pillarHeight_);
    tipOrb_->SetColor(color);
  }
}

void ClearSequence::Draw3D() {
  if (!isActive_) {
    return;
  }
  // カメラはこのフレームの描画のカメラを使うので、描く直前に行列を求める
  if (isMoteVisible_) {
    for (std::unique_ptr<Object3d> &mote : motes_) {
      mote->Update();
      mote->Draw();
    }
  }
  if (isOrbVisible_) {
    orb_->Update();
    orb_->Draw();
  }
  if (isPillarVisible_) {
    pillar_->Update();
    pillar_->Draw();
  }
  if (isTipOrbVisible_) {
    tipOrb_->Update();
    tipOrb_->Draw();
  }
}

void ClearSequence::Draw2D() {
  if (!isActive_) {
    return;
  }
  letterboxTop_->Draw();
  letterboxBottom_->Draw();
  card_->Draw();
}

//===========================
// ImGui
//===========================

void ClearSequence::DrawDebugLines(LineRenderer &lines) const {
  if (IsDragonFlying()) {
    flight_->DrawDebugLines(lines);
  }
}

bool ClearSequence::GetTopView(Vector3 &translate, Vector3 &rotate) const {
  if (!IsDragonFlying()) {
    return false;
  }
  flight_->GetTopView(translate, rotate);
  return true;
}

void ClearSequence::DrawImGui() {
#ifdef USE_IMGUI
  // 今の場面の名前
  const char *scene = "Result";
  if (timer_ < ApproachStart()) {
    scene = "0 Hit";
  } else if (timer_ < BlastStart()) {
    scene = "1 Break";
  } else if (timer_ < GatherStart()) {
    scene = "2 Blast";
  } else if (timer_ < PillarStart()) {
    scene = "3 Gather";
  } else if (timer_ < ConvergeStart()) {
    scene = "4 Pillar";
  } else if (timer_ < HoldStart()) {
    scene = "5 Converge";
  } else if (timer_ < BurstStart()) {
    scene = "6 Hold";
  } else if (timer_ < AfterglowStart()) {
    scene = "7 Burst";
  } else if (timer_ < ResultTime()) {
    scene = "8 Afterglow";
  }
  ImGui::Text("Time %.2f / %.2f  [%s]  scale %.2f", timer_, ResultTime(), scene,
              GetTimeScale());
  if (ImGui::Button("Save Clear Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Clear Config")) {
    LoadConfig();
  }
  // 止めて、つまみで時刻を動かし、その時刻の構図を見る
  ImGui::Checkbox("Pause", &isPaused_);
  if (isPaused_) {
    ImGui::SliderFloat("Scrub Time", &timer_, 0.0f, ResultTime() + 2.0f);
  }

  ImGui::SeparatorText("Timing (sec)");
  ImGui::DragFloat("Hit", &hitSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Hit Stop", &hitStopSec_, 0.01f, 0.0f, 1.0f);
  ImGui::SliderFloat("Slow Scale", &slowScale_, 0.05f, 1.0f);
  ImGui::DragFloat("Slow Recover", &slowRecoverSec_, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Approach", &approachSec_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Break", &breakSec_, 0.01f, 0.0f, 8.0f);
  ImGui::DragFloat("Blast", &blastSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Gather", &gatherSec_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Pillar", &pillarSec_, 0.01f, 0.05f, 5.0f);
  ImGui::DragFloat("Converge", &convergeSec_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Hold", &holdSec_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Clear", &clearSec_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Afterglow", &afterglowSec_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Skip Lock", &skipLockSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("HUD Fade", &hudFadeSec_, 0.01f, 0.0f, 2.0f);

  ImGui::SeparatorText("Letterbox");
  ImGui::DragFloat("Aspect", &letterboxAspect_, 0.01f, 1.78f, 3.0f);
  ImGui::DragFloat("In Sec", &letterboxInSec_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Out Sec", &letterboxOutSec_, 0.01f, 0.0f, 2.0f);

  ImGui::SeparatorText("Spread");
  ImGui::SliderFloat("Spread Start Deg", &spreadStartDeg_, 0.0f, 90.0f);
  ImGui::DragFloat("Sun Boost", &sunBoost_, 0.05f, 1.0f, 20.0f);
  ImGui::DragFloat("Sun Boost Fade Sec", &sunBoostFadeSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Burst Sec", &burstSec_, 0.01f, 0.05f, 5.0f);
  ImGui::SliderFloat("Frame Out Deg", &frameOutDeg_, 0.0f, 150.0f);
  ImGui::DragFloat("Finish Sec", &finishSec_, 0.01f, 0.05f, 5.0f);
  ImGui::Text("radius %.1f deg  spread %.2f  edge %.2f  sun x%.2f",
              SpreadRadiusDegAt(timer_), SpreadAt(timer_), EdgeDarkAt(timer_),
              SunBoostAt(timer_));

  if (ImGui::TreeNode("Camera")) {
    ImGui::DragFloat3("Near Offset", &nearCameraOffset_.x, 0.01f);
    ImGui::DragFloat3("Near Look Offset", &nearLookOffset_.x, 0.01f);
    ImGui::SliderFloat("Near Fov", &nearFov_, 0.1f, 2.0f);
    ImGui::SliderFloat("Approach Look Ratio", &approachLookRatio_, 0.05f, 1.0f);
    ImGui::SliderFloat("Push In", &pushIn_, 0.3f, 1.2f);
    ImGui::DragFloat("Break Shake", &breakShake_, 0.001f, 0.0f, 0.5f);
    ImGui::DragFloat("Blast Shake", &blastShake_, 0.001f, 0.0f, 0.5f);
    ImGui::DragFloat("Blast Shake Sec", &blastShakeSec_, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat3("Far Offset", &farCameraOffset_.x, 0.05f);
    ImGui::SliderFloat("Far Pitch Deg", &farPitchDeg_, -10.0f, 85.0f);
    ImGui::SliderFloat("Far Yaw Deg", &farYawDeg_, -60.0f, 60.0f);
    ImGui::SliderFloat("Far Fov", &farFov_, 0.1f, 2.0f);
    ImGui::DragFloat("Launch Shake", &launchShake_, 0.001f, 0.0f, 0.5f);
    ImGui::DragFloat("Launch Shake Sec", &launchShakeSec_, 0.01f, 0.0f, 2.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Boss")) {
    ImGui::Text("explosions %d  flash %.2f", int(ExplosionTimes().size()),
                FlashAt(timer_));
    ImGui::SeparatorText("Explosion");
    ImGui::DragFloat("First Delay", &explosionFirstDelay_, 0.01f, 0.0f, 3.0f);
    ImGui::DragFloat("Interval", &explosionInterval_, 0.01f, 0.02f, 2.0f);
    ImGui::SliderFloat("Interval Ratio", &explosionIntervalRatio_, 0.3f, 1.0f);
    ImGui::DragFloat("Min Interval", &explosionMinInterval_, 0.005f, 0.02f, 1.0f);
    ImGui::DragFloat("End Gap", &explosionEndGap_, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Scale Start", &explosionScaleStart_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Scale End", &explosionScaleEnd_, 0.01f, 0.0f, 5.0f);
    ImGui::ColorEdit3("Explosion Color", &explosionColor_.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::SeparatorText("Tilt");
    ImGui::DragFloat("Kick Deg", &tiltKickDeg_, 0.1f, 0.0f, 45.0f);
    ImGui::SliderFloat("Jitter", &tiltJitter_, 0.0f, 1.0f);
    ImGui::DragFloat("Rise Sec", &tiltRiseSec_, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Recover Sec", &tiltRecoverSec_, 0.01f, 0.01f, 2.0f);
    ImGui::SliderFloat("Keep", &tiltKeep_, 0.0f, 1.0f);
    ImGui::DragFloat("Max Deg", &tiltMaxDeg_, 0.5f, 0.0f, 90.0f);
    ImGui::SeparatorText("Flash");
    ImGui::ColorEdit3("Flash Color", &flashColor_.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Peak", &flashPeak_, 0.0f, 1.0f);
    ImGui::DragFloat("Flash Sec", &flashSec_, 0.005f, 0.01f, 1.0f);
    ImGui::SliderFloat("Floor", &flashFloor_, 0.0f, 1.0f);
    ImGui::SeparatorText("Blast");
    ImGui::DragFloat("Blast Scale", &blastScale_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Blast Point Scale", &blastPointScale_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Debris Speed", &debrisSpeed_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Debris Gravity", &debrisGravity_, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat("Debris Drag", &debrisDrag_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Debris Spin", &debrisSpin_, 0.05f, 0.0f, 20.0f);
    ImGui::ColorEdit3("Debris Rim Color", &debrisRimColor_.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Debris Rim Intensity", &debrisRimIntensity_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Debris Rim Power", &debrisRimPower_, 0.05f, 0.1f, 10.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Light")) {
    ImGui::ColorEdit3("Color", &glowColor_.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Mote Distance", &moteDistance_, 0.01f, 0.0f, 5.0f);
    ImGui::SliderFloat("Mote Distance Jitter", &moteDistanceJitter_, 0.0f, 1.0f);
    ImGui::DragFloat("Mote Out Sec", &moteOutSec_, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Mote Return Sec", &moteReturnSec_, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Mote Scale", &moteScale_, 0.001f, 0.0f, 0.5f);
    ImGui::DragFloat("Mote Intensity", &moteIntensity_, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Orb Start Scale", &orbStartScale_, 0.005f, 0.0f, 2.0f);
    ImGui::DragFloat("Orb Max Scale", &orbMaxScale_, 0.005f, 0.0f, 2.0f);
    ImGui::DragFloat("Orb Intensity", &orbIntensity_, 0.05f, 0.0f, 20.0f);
    ImGui::ColorEdit3("Orb Rim Color", &orbRimColor_.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat("Orb Rim Intensity", &orbRimIntensity_, 0.05f, 0.0f, 20.0f);
    ImGui::DragFloat("Orb Rim Power", &orbRimPower_, 0.05f, 0.1f, 16.0f);
    ImGui::DragFloat("Squeeze Sec", &squeezeSec_, 0.01f, 0.0f, 1.0f);
    ImGui::SliderFloat("Squeeze Scale", &squeezeScale_, 0.05f, 1.0f);
    ImGui::DragFloat("Orb Shrink Sec", &orbShrinkSec_, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Pillar Radius", &pillarRadius_, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Pillar Height", &pillarHeight_, 1.0f, 10.0f, 1800.0f);
    ImGui::DragFloat("Pillar Intensity", &pillarIntensity_, 0.05f, 0.0f, 20.0f);
    ImGui::SliderFloat("Pillar Pulse", &pillarPulse_, 0.0f, 0.5f);
    ImGui::DragFloat("Tip Orb Scale", &tipOrbScale_, 0.1f, 0.0f, 200.0f);
    ImGui::SliderFloat("Tip Orb Start Ratio", &tipOrbStartRatio_, 0.0f, 1.0f);
    ImGui::SliderFloat("Tip Shrink Scale", &tipShrinkScale_, 0.0f, 1.0f);
    ImGui::DragFloat("Burst Orb Scale", &burstOrbScale_, 1.0f, 0.0f, 2000.0f);
    ImGui::DragFloat("Burst Orb Sec", &burstOrbSec_, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Tip Orb Intensity", &tipOrbIntensity_, 0.05f, 0.0f,
                     20.0f);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Dragon")) {
    ImGui::DragFloat("Enter Sec", &dragonEnterSec_, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Back Rim Scale", &dragonBackRimScale_, 0.01f, 0.0f, 5.0f);
    flight_->DrawImGui();
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Card")) {
    ImGui::DragFloat("Grow Sec", &cardGrowSec_, 0.01f, 0.01f, 3.0f);
    ImGui::DragFloat("Lower Delay", &cardLowerDelay_, 0.01f, 0.0f, 2.0f);
    ImGui::DragFloat("Out Sec", &cardOutSec_, 0.01f, 0.01f, 2.0f);
    ImGui::DragFloat("Line Y", &cardLineY_, 1.0f, 0.0f, 720.0f);
    ImGui::DragFloat("Line Width", &cardLineWidth_, 1.0f, 0.0f, 1280.0f);
    ImGui::TreePop();
  }
#endif // USE_IMGUI
}
