#include "GameOverSequence.h"
#include "CaptionCard.h"
#include "Camera/GameCamera.h"
#include "Camera/RailCamera.h"
#include "Core/WindowSystem.h"
#include "Sprite/Sprite.h"
#include "../../externals/nlohmann/json.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 調整値の保存先
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/GameOverConfig.json";
// 使う一打ちの数の JSON のキー
const char *const kBeatCountKey = "beatCount";

// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
// UV は画像全体をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

/// <summary>
/// 「GAME OVER」のカードの時刻と置き場所（時刻は被弾からの秒）
/// 翼が畳まれきった所で見下ろしに出て、黒い幕が下りたあとも黒の上に出たまま読ませる
/// 縦の位置は、雲海へ沈むドラゴン（画面の高さの 0.45〜0.5）より上
/// </summary>
CaptionCard::Settings MakeGameOverCardSettings() {
  CaptionCard::Settings settings;
  settings.upperPath = "resources/UI/GameOver/gameover_title.png";
  settings.lowerPath = "resources/UI/GameOver/gameover_result.png";
  settings.cardInTime = 2.6f;
  settings.cardOutTime = 5.4f;
  settings.lineY = 240.0f;
  settings.lineWidth = 590.0f;
  settings.upperY = 186.0f;
  settings.lowerY = 280.0f;
  // 横線は下の言葉と同じ、くすんだ金色
  settings.lineColor = {0.71f, 0.65f, 0.51f, 1.0f};
  return settings;
}

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

/// <summary>
/// 羽ばたきの状態を割合 t で混ぜる
/// </summary>
DragonWingAnimator::State LerpWing(const DragonWingAnimator::State &a,
                                   const DragonWingAnimator::State &b,
                                   float t) {
  DragonWingAnimator::State result;
  result.strength = Lerp(a.strength, b.strength, t);
  result.frequency = Lerp(a.frequency, b.frequency, t);
  result.fold = Lerp(a.fold, b.fold, t);
  result.asymmetry = Lerp(a.asymmetry, b.asymmetry, t);
  result.pose = Lerp(a.pose, b.pose, t);
  result.hover = Lerp(a.hover, b.hover, t);
  result.sideLag = Lerp(a.sideLag, b.sideLag, t);
  result.buckle = Lerp(a.buckle, b.buckle, t);
  result.flail = Lerp(a.flail, b.flail, t);
  return result;
}

/// <summary>
/// 落ちる速さの上限を 1 としたときの、動きの時間 motionTime までに下がった距離
/// 速さは accelSec かけて 0 から上限まで一定の割合で上がり、そのあとは一定
/// </summary>
float UnitFallDistance(float motionTime, float accelSec) {
  if (motionTime <= 0.0f) {
    return 0.0f;
  }
  if (accelSec <= 0.0f) {
    return motionTime;
  }
  if (motionTime < accelSec) {
    return 0.5f * motionTime * motionTime / accelSec;
  }
  return motionTime - 0.5f * accelSec;
}
} // namespace

GameOverSequence::GameOverSequence()
    : camera_(std::make_unique<GameCamera>()) {}

GameOverSequence::~GameOverSequence() = default;

void GameOverSequence::Initialize(SpriteRenderer *spriteRenderer) {
  curtain_ = std::make_unique<Sprite>();
  curtain_->Initialize(spriteRenderer, "resources/white1x1.png");
  curtain_->SetAnchorPoint({0.0f, 0.0f});
  curtain_->SetPosition({0.0f, 0.0f});
  curtain_->SetSize({static_cast<float>(WindowSystem::kClientWidth),
                     static_cast<float>(WindowSystem::kClientHeight)});

  card_ = std::make_unique<CaptionCard>();
  card_->Initialize(spriteRenderer, MakeGameOverCardSettings());

  LoadConfig();
}

void GameOverSequence::VisitConfigValues(
    const std::function<void(const std::string &key, float &value)> &visit) {
  // 時間
  visit("slowSec", slowSec_);
  visit("slowScale", slowScale_);
  visit("slowRecoverSec", slowRecoverSec_);
  visit("limpStartTime", limpStartTime_);
  visit("limpEndTime", limpEndTime_);
  visit("pullStartTime", pullStartTime_);
  visit("sinkTime", sinkTime_);
  visit("darkenStartTime", darkenStartTime_);
  visit("darkenSec", darkenSec_);
  visit("skipLockSec", skipLockSec_);
  visit("hudFadeSec", hudFadeSec_);

  // ドラゴンの動き（角度はラジアン）
  visit("hitPushDistance", hitPushDistance_);
  visit("hitLift", hitLift_);
  visit("hitPushSec", hitPushSec_);
  visit("driftSpeed", driftSpeed_);
  visit("driftDecaySec", driftDecaySec_);
  visit("fallAccelSec", fallAccelSec_);
  visit("sinkDepth", sinkDepth_);
  visit("minFallDistance", minFallDistance_);
  visit("hitRoll", hitRoll_);
  visit("limpRoll", limpRoll_);
  visit("hitPitch", hitPitch_);
  visit("limpPitch", limpPitch_);
  visit("sinkPitch", sinkPitch_);
  visit("struggleRoll", struggleRoll_);
  visit("liftSpeed", liftSpeed_);
  visit("liftReturnSec", liftReturnSec_);
  visit("heavePitch", heavePitch_);

  // 翼
  visit("hitFold", hitWing_.fold);
  visit("hitHover", hitWing_.hover);
  visit("struggleFold", struggleWing_.fold);
  visit("struggleHover", struggleWing_.hover);
  visit("limpFold", limpWing_.fold);
  visit("limpHover", limpWing_.hover);
  visit("hitRiseSec", hitRiseSec_);
  visit("hitUpAngle", hitUpAngle_);
  for (size_t i = 0; i < kMaxBeatCount_; ++i) {
    StruggleBeat &beat = beats_[i];
    const std::string prefix = "beat" + std::to_string(i + 1) + ".";
    visit(prefix + "time", beat.time);
    visit(prefix + "downSec", beat.downSec);
    visit(prefix + "downAngle", beat.downAngle);
    visit(prefix + "upSec", beat.upSec);
    visit(prefix + "leftUpAngle", beat.leftUpAngle);
    visit(prefix + "rightUpAngle", beat.rightUpAngle);
    visit(prefix + "rightDelay", beat.rightDelay);
    visit(prefix + "buckle", beat.buckle);
    visit(prefix + "flail", beat.flail);
    visit(prefix + "lift", beat.lift);
    visit(prefix + "tremble", beat.tremble);
  }
  visit("foldRootAngle", foldRootAngle_);
  visit("faceUpAngle", faceUpAngle_);
  visit("hangStartTime", hangStartTime_);
  visit("hangSec", hangSec_);
  visit("neckTrembleRatio", neckTrembleRatio_);
  visit("struggleResponse", struggleResponse_);

  // 脚
  visit("paddleStartTime", paddleStartTime_);
  visit("paddleRiseSec", paddleRiseSec_);
  visit("paddleTiredTime", paddleTiredTime_);
  visit("paddleTiredStrength", paddleTiredStrength_);
  visit("paddleStopTime", paddleStopTime_);
  visit("paddleStartSec", paddleStartSec_);
  visit("paddleEndSec", paddleEndSec_);
  visit("lastPaddleTime", lastPaddleTime_);
  visit("lastPaddleSec", lastPaddleSec_);
  visit("lastPaddleLegDelay", lastPaddleLegDelay_);
  visit("lastPaddleStrength", lastPaddleStrength_);
  for (size_t leg = 0; leg < lastPaddleLegWeights_.size(); ++leg) {
    visit("lastPaddleLegWeight" + std::to_string(leg + 1),
          lastPaddleLegWeights_[leg]);
  }

  // 頭
  visit("headFollow", headFollow_);
  visit("struggleFaceUpScale", struggleFaceUpScale_);

  // カメラ
  visit("sideMoveStartTime", sideMoveStartTime_);
  visit("sideMoveSec", sideMoveSec_);
  auto visitVector = [&](const std::string &key, Vector3 &value) {
    visit(key + ".x", value.x);
    visit(key + ".y", value.y);
    visit(key + ".z", value.z);
  };
  visitVector("sideCameraOffset", sideCameraOffset_);
  visitVector("sideLookOffset", sideLookOffset_);
  visitVector("pullCameraOffset", pullCameraOffset_);
  visit("pullSec", pullSec_);
  visit("hitShake", hitShake_);
  visit("hitShakeSec", hitShakeSec_);
}

void GameOverSequence::LoadConfig() {
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
  if (root.contains(kBeatCountKey) && root[kBeatCountKey].is_number_integer()) {
    beatCount_ = root[kBeatCountKey].get<int>();
  }
}

void GameOverSequence::SaveConfig() {
  nlohmann::json root;
  VisitConfigValues(
      [&](const std::string &key, float &value) { root[key] = value; });
  root[kBeatCountKey] = beatCount_;
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void GameOverSequence::Start(const RailCamera &railCamera,
                             const Vector3 &dragonPos,
                             const Vector3 &dragonRot, float seaHeight) {
  isActive_ = true;
  isTransitionCued_ = false;
  isCueReady_ = false;
  timer_ = 0.0f;
  motionTime_ = 0.0f;

  startPos_ = dragonPos;
  startRot_ = dragonRot;
  dragonPos_ = dragonPos;
  dragonRot_ = dragonRot;

  // レールの右・前を水平にする
  Vector3 forward = railCamera.GetRailForward();
  railForward_ = Normalize(Vector3{forward.x, 0.0f, forward.z});
  railRight_ = Normalize(Cross(kWorldUp, railForward_));

  // レールカメラの位置・画角と、見ている点
  // ビュー行列の逆行列（カメラのワールド行列）の3行目がカメラの正面方向、4行目が位置
  const float kRailLookDistance = 10.0f;
  Matrix4x4 railWorld = Inverse(railCamera.GetViewMatrix());
  Vector3 railLookDir = {railWorld.m[2][0], railWorld.m[2][1],
                         railWorld.m[2][2]};
  railCameraPos_ = {railWorld.m[3][0], railWorld.m[3][1], railWorld.m[3][2]};
  railLookPos_ = railCameraPos_ + Normalize(railLookDir) * kRailLookDistance;
  railCameraFov_ = railCamera.GetFov();

  // sinkTime_ に、雲海の面から sinkDepth_ だけ沈んでいるように、落ちる速さの上限を決める
  float fallDistance =
      (std::max)(dragonPos.y - (seaHeight - sinkDepth_), minFallDistance_);
  float unitDistance =
      UnitFallDistance(MotionTimeAt(sinkTime_), fallAccelSec_);
  fallSpeed_ = fallDistance / (std::max)(unitDistance, kFrameSec);

  wingState_ = hitWing_;
  gesture_ = {};
  leftPush_ = 0.0f;
  rightPush_ = 0.0f;
  lift_ = 0.0f;
  beatAngles_ = {};
  curtainAlpha_ = 0.0f;
  card_->Start();
  UpdateCamera();
}

void GameOverSequence::Stop() {
  isActive_ = false;
  isCueReady_ = false;
  gesture_ = {};
  curtainAlpha_ = 0.0f;
  card_->Stop();
}

void GameOverSequence::Update(bool isSkipPressed) {
  if (!isActive_) {
    return;
  }

  // 止めている間は、つまみで決めた時刻の姿を映し、合図は出さない
  if (isPaused_) {
    motionTime_ = MotionTimeAt(timer_);
    UpdateDragon();
    UpdateWing();
    UpdateCamera();
    UpdateScreen();
    return;
  }

  const float motionDelta = kFrameSec * GetTimeScale();
  motionTime_ += motionDelta;
  timer_ += kFrameSec;

  // 打ち下ろしで浮き、浮いた高さは落ちる道筋へ戻っていく
  const float push = 0.5f * (leftPush_ + rightPush_);
  lift_ += liftSpeed_ * CurrentBeat().lift * push * motionDelta;
  lift_ *= std::exp(-motionDelta / liftReturnSec_);

  // カードが閉じきった時刻か、決定キーで、拠点への遷移の合図を出す
  const bool canSkip = isSkipPressed && timer_ >= skipLockSec_;
  if (!isTransitionCued_ && (timer_ >= card_->GetEndTime() || canSkip)) {
    isTransitionCued_ = true;
    isCueReady_ = true;
  }

  UpdateDragon();
  UpdateWing();
  UpdateCamera();
  UpdateScreen();
}

void GameOverSequence::UpdateScreen() {
  //===========================
  // 黒い幕：カードが出たあと、その後ろで場面が暗くなっていき、真っ黒のまま
  //===========================
  curtainAlpha_ =
      EaseInOutQuad(Progress(timer_, darkenStartTime_, darkenSec_));
  curtain_->SetColor({0.0f, 0.0f, 0.0f, curtainAlpha_});
  curtain_->Update(kUvTransform);

  // カードは演出の時計に合わせる（止めてつまみで時刻を動かしても同じ姿になる）
  card_->SetTime(timer_);
}

void GameOverSequence::Draw() {
  if (!isActive_) {
    return;
  }
  if (curtainAlpha_ > 0.0f) {
    curtain_->Draw();
  }
  card_->Draw();
}

bool GameOverSequence::ConsumeTransitionCue() {
  if (!isCueReady_) {
    return false;
  }
  isCueReady_ = false;
  return true;
}

float GameOverSequence::GetTimeScale() const {
  if (!isActive_) {
    return 1.0f;
  }
  // slowSec_ の終わりの slowRecoverSec_ の間に、ゆっくりから普通の速さへ戻す
  const float recoverStart = slowSec_ - slowRecoverSec_;
  return Lerp(slowScale_, 1.0f, Progress(timer_, recoverStart, slowRecoverSec_));
}

float GameOverSequence::MotionTimeAt(float time) const {
  // GetTimeScale の速さを 0 から time まで足し合わせたもの
  const float recoverStart = (std::max)(slowSec_ - slowRecoverSec_, 0.0f);
  const float recoverSec = slowSec_ - recoverStart;
  if (time <= recoverStart) {
    return slowScale_ * time;
  }
  float motion = slowScale_ * recoverStart;
  if (time <= slowSec_ && recoverSec > 0.0f) {
    // 速さは slowScale_ から 1 へ一定の割合で上がるので、台形の面積になる
    float elapsed = time - recoverStart;
    float scaleNow = Lerp(slowScale_, 1.0f, elapsed / recoverSec);
    return motion + 0.5f * (slowScale_ + scaleNow) * elapsed;
  }
  motion += 0.5f * (slowScale_ + 1.0f) * recoverSec;
  return motion + (time - slowSec_);
}

float GameOverSequence::GetHudAlpha() const {
  if (!isActive_) {
    return 1.0f;
  }
  return 1.0f - Progress(timer_, 0.0f, hudFadeSec_);
}

const ICamera *GameOverSequence::GetCamera() const { return camera_.get(); }

Vector3 GameOverSequence::ToWorld(const Vector3 &base,
                                  const Vector3 &offset) const {
  return base + railRight_ * offset.x + kWorldUp * offset.y +
         railForward_ * offset.z;
}

Vector3 GameOverSequence::DragonPositionAt(float motionTime) const {
  // 右へ弾かれて少し浮き、前へ進む勢いが落ちながら、速さを上げて落ちる
  const float push = EaseOutCubic(Progress(motionTime, 0.0f, hitPushSec_));
  // 浮き上がりは弾かれる時間の2倍で、上がって戻る（sin の半周）
  const float liftPhase = Progress(motionTime, 0.0f, hitPushSec_ * 2.0f);
  const float lift = hitLift_ * std::sin(std::numbers::pi_v<float> * liftPhase);
  const float drift = driftSpeed_ * driftDecaySec_ *
                      (1.0f - std::exp(-motionTime / driftDecaySec_));
  return ToWorld(startPos_, {hitPushDistance_ * push, lift, drift}) -
         kWorldUp * FallDistance(motionTime);
}

float GameOverSequence::FallDistance(float motionTime) const {
  return fallSpeed_ * UnitFallDistance(motionTime, fallAccelSec_);
}

void GameOverSequence::UpdateDragon() {
  //===========================
  // 位置：落ちる道筋に、打ち下ろしで浮いた高さを足す
  //===========================
  dragonPos_ = DragonPositionAt(motionTime_) + kWorldUp * lift_;
  const float hitPush =
      EaseOutCubic(Progress(motionTime_, 0.0f, hitPushSec_));

  //===========================
  // 向き：弾かれて傾き機首が上がる → もがく間に力が抜けて機首が下がる → 雲へ頭から沈む
  //===========================
  const float struggleStartTime = beats_.front().time;
  const float struggleToLimp = EaseInOutQuad(
      Progress(timer_, struggleStartTime, limpEndTime_ - struggleStartTime));
  const float limpToSink = EaseInOutQuad(
      Progress(timer_, limpEndTime_, sinkTime_ - limpEndTime_));

  float roll = hitRoll_ * hitPush;
  float pitch = Lerp(startRot_.x, hitPitch_, hitPush);
  if (timer_ >= struggleStartTime) {
    roll = Lerp(hitRoll_, limpRoll_, struggleToLimp);
    pitch = Lerp(hitPitch_, limpPitch_, struggleToLimp);
  }
  if (timer_ >= limpEndTime_) {
    pitch = Lerp(limpPitch_, sinkPitch_, limpToSink);
  }

  // もがく間：左右の打ち下ろしの差で体が傾き、打ち下ろしで機首が上がる。力が抜けると止まる
  if (timer_ >= struggleStartTime) {
    const float active =
        1.0f - Progress(timer_, limpStartTime_, limpEndTime_ - limpStartTime_);
    const float push = 0.5f * (leftPush_ + rightPush_);
    roll += struggleRoll_ * (leftPush_ - rightPush_) * active;
    pitch -= heavePitch_ * CurrentBeat().lift * push * active;
  }

  dragonRot_ = {pitch, startRot_.y, startRot_.z + roll};
}

void GameOverSequence::RecordWingAngle(float left, float right) {
  // もがく間だけ、今の一打ちの最小・最大を更新する
  if (!isActive_ || timer_ < beats_.front().time || timer_ >= limpStartTime_) {
    return;
  }
  const size_t index = static_cast<size_t>(&CurrentBeat() - beats_.data());
  WingAngleRange &range = beatAngles_[index];
  if (!range.isRecorded) {
    range = {left, left, right, right, true};
    return;
  }
  range.leftMin = (std::min)(range.leftMin, left);
  range.leftMax = (std::max)(range.leftMax, left);
  range.rightMin = (std::min)(range.rightMin, right);
  range.rightMax = (std::max)(range.rightMax, right);
}

size_t GameOverSequence::UsedBeatCount() const {
  return static_cast<size_t>(
      std::clamp(beatCount_, 1, static_cast<int>(kMaxBeatCount_)));
}

const GameOverSequence::StruggleBeat &
GameOverSequence::BeatAt(float time) const {
  // 使う一打ちのうち、始める時刻を過ぎた最後のもの（もがき始める前は最初の一打ち）
  const StruggleBeat *current = &beats_.front();
  for (size_t i = 0; i < UsedBeatCount(); ++i) {
    if (time >= beats_[i].time) {
      current = &beats_[i];
    }
  }
  return *current;
}

float GameOverSequence::WingRootAngle(bool isRight, float delaySec) const {
  float time = timer_ - delaySec;
  // 右の翼は、その時刻に打っている一打ちの遅れの分だけ前の角度にする
  if (isRight) {
    time -= BeatAt(time).rightDelay;
  }
  return RootAngleAt(isRight, time);
}

float GameOverSequence::RootAngleAt(bool isRight, float time) const {
  if (time < limpStartTime_) {
    return StruggleRootAngleAt(isRight, time);
  }
  // 畳まれる：もがき終わった角度から、畳まれたときの角度へ
  const float limp = EaseInOutQuad(
      Progress(time, limpStartTime_, limpEndTime_ - limpStartTime_));
  return Lerp(StruggleRootAngleAt(isRight, limpStartTime_), foldRootAngle_,
              limp);
}

float GameOverSequence::StruggleRootAngleAt(bool isRight, float time) const {
  // 被弾：弾かれて上げきりまで跳ね上がる。速く出て、最初の一打ちの始まりで上げきる（被弾の前は被弾した瞬間の角度）
  if (time < beats_.front().time) {
    const float startAngle = isRight ? startRightRoot_ : startLeftRoot_;
    return Lerp(startAngle, hitUpAngle_,
                EaseOutQuad(Progress(time, 0.0f, hitRiseSec_)));
  }

  // 一打ち：前の一打ちの上げきりから下ろしきりへ打ち下ろし、上げきりへ打ち上げて止まる
  // どちらも始めと終わりがゆっくりの曲線でつなぐ
  const StruggleBeat &beat = BeatAt(time);
  const size_t index = static_cast<size_t>(&beat - beats_.data());
  auto upAngleOf = [isRight](const StruggleBeat &b) {
    return isRight ? b.rightUpAngle : b.leftUpAngle;
  };
  const float topAngle =
      (index == 0) ? hitUpAngle_ : upAngleOf(beats_[index - 1]);
  const float downEnd = beat.time + beat.downSec;
  if (time < downEnd) {
    return Lerp(topAngle, beat.downAngle,
                EaseInOutQuad(Progress(time, beat.time, beat.downSec)));
  }
  return Lerp(beat.downAngle, upAngleOf(beat),
              EaseInOutQuad(Progress(time, downEnd, beat.upSec)));
}

void GameOverSequence::UpdateWing() {
  //===========================
  // 付け根の角度は WingRootAngle が一打ちの表から決める。ここでは畳み具合・折れ・振り回され・首を決める
  // 被弾 → 一打ちごとに折れ・振り回されが変わる → 畳まれて体の力が抜ける
  //===========================
  const StruggleBeat &beat = CurrentBeat();
  const float limp = EaseInOutQuad(
      Progress(timer_, limpStartTime_, limpEndTime_ - limpStartTime_));
  DragonWingAnimator::State struggle = hitWing_;
  if (timer_ >= beats_.front().time) {
    struggle = struggleWing_;
    struggle.buckle = beat.buckle;
    struggle.flail = beat.flail;
  }
  wingState_ = LerpWing(struggle, limpWing_, limp);
  wingState_.response = struggleResponse_;

  // 顔は、被弾から最初の一打ちまでに空の方へ向け、体の傾きによらず沈みきるまで同じ向きに保つ
  // 脚は空を掻いてから止まり、脚と尾は重さで垂れていく。最後の打ち下ろしで、垂れたまま脚が弱く一掻きする
  // もがく間は、空へ向ける割合を弱め、頭を体の揺れについていかせる
  gesture_ = {};
  const float headShake = HeadShakeAt(timer_);
  gesture_.faceUp = EaseOutQuad(Progress(timer_, 0.0f, beats_.front().time)) *
                    Lerp(1.0f, struggleFaceUpScale_, headShake);
  gesture_.faceUpAngle = faceUpAngle_;
  gesture_.headFollow = headFollow_ * headShake;
  gesture_.hang = EaseOutQuad(Progress(timer_, hangStartTime_, hangSec_));
  gesture_.paddle = PaddleStrengthAt(timer_);
  gesture_.paddlePhase = PaddlePhaseAt(timer_);
  for (size_t leg = 0; leg < gesture_.limpLegSwing.size(); ++leg) {
    gesture_.limpLegSwing[leg] =
        LastPaddleSwingAt(static_cast<int>(leg), timer_);
  }

  // 打ち上げの間は指と首が震え、上げ始めと上げきりで 0、真ん中で一番強い。畳まれ始めたら弱めて 0 にする
  if (timer_ >= beats_.front().time) {
    const float upProgress =
        Progress(timer_, beat.time + beat.downSec, beat.upSec);
    gesture_.tremble = beat.tremble *
                       std::sin(std::numbers::pi_v<float> * upProgress) *
                       (1.0f - limp);
    gesture_.neckTrembleRatio = neckTrembleRatio_;
  }
}

float GameOverSequence::PaddleStrengthAt(float time) const {
  // 0 → 1 へ素早く上げ、paddleTiredStrength_ まで一定の割合で下げてから、0 へ止める
  const float riseEnd = paddleStartTime_ + paddleRiseSec_;
  if (time < riseEnd) {
    return EaseOutQuad(Progress(time, paddleStartTime_, paddleRiseSec_));
  }
  if (time < paddleTiredTime_) {
    return Lerp(1.0f, paddleTiredStrength_,
                Progress(time, riseEnd, paddleTiredTime_ - riseEnd));
  }
  return Lerp(paddleTiredStrength_, 0.0f,
              EaseOutQuad(Progress(time, paddleTiredTime_,
                                   paddleStopTime_ - paddleTiredTime_)));
}

float GameOverSequence::PaddlePhaseAt(float time) const {
  // 一掻きの長さ: 強さが 1 になるまでは paddleStartSec_、そこから下がりきるまでに一定の割合で paddleEndSec_ へ延ばし、そのあとはそのまま
  // 周期の数は、経過時間を一掻きの長さで割ったものを足し合わせる
  const float kMinPaddleSec = 0.05f; // 一掻きの長さの下限（秒）
  const float kMinRate = 1.0e-4f;    // これより小さい延び方は、長さが変わらないとみなす
  const float startSec = (std::max)(paddleStartSec_, kMinPaddleSec);
  const float endSec = (std::max)(paddleEndSec_, kMinPaddleSec);
  const float riseEnd = paddleStartTime_ + paddleRiseSec_;
  if (time <= riseEnd) {
    return (time - paddleStartTime_) / startSec;
  }
  float cycles = paddleRiseSec_ / startSec;

  // 長さ startSec + rate × s を、s で 1 / 長さ を積分すると log(長さ / startSec) / rate になる
  const float slowSec = (std::max)(paddleTiredTime_ - riseEnd, 0.0f);
  const float slowElapsed = (std::min)(time - riseEnd, slowSec);
  const float rate = slowSec > 0.0f ? (endSec - startSec) / slowSec : 0.0f;
  if (std::abs(rate) > kMinRate) {
    cycles += std::log((startSec + rate * slowElapsed) / startSec) / rate;
  } else {
    cycles += slowElapsed / startSec;
  }
  const float afterSlow = time - riseEnd - slowSec;
  if (afterSlow > 0.0f) {
    cycles += afterSlow / endSec;
  }
  return cycles;
}

float GameOverSequence::LastPaddleSwingAt(int leg, float time) const {
  // 掻く順（左前 → 右後 → 右前 → 左後）での、各脚の順番（左前・右前・左後・右後の順に並べたもの）
  const int kLegOrder[] = {0, 2, 3, 1};
  const float start = lastPaddleTime_ + lastPaddleLegDelay_ * kLegOrder[leg];
  const float progress = Progress(time, start, lastPaddleSec_);
  if (progress <= 0.0f || progress >= 1.0f) {
    return 0.0f;
  }
  // 一周期の sin で、前へ出して（負）後ろへ引き（正）、垂れた位置に戻す
  return -std::sin(2.0f * std::numbers::pi_v<float> * progress) *
         lastPaddleStrength_ * lastPaddleLegWeights_[leg];
}

float GameOverSequence::HeadShakeAt(float time) const {
  // 掻き始めから強さが 1 になるまでに 0 → 1、畳まれる間に 1 → 0
  const float rise =
      EaseOutQuad(Progress(time, paddleStartTime_, paddleRiseSec_));
  const float fall = EaseInOutQuad(
      Progress(time, limpStartTime_, limpEndTime_ - limpStartTime_));
  return rise * (1.0f - fall);
}

void GameOverSequence::UpdateCamera() {
  //===========================
  // レールカメラの位置から、ドラゴンの横へ回り込み、一緒に落ちる
  //===========================
  // 落ちる道筋を追い、打ち下ろしで浮いた高さは追わない
  const Vector3 pathPos = DragonPositionAt(motionTime_);
  const Vector3 sideCameraPos = ToWorld(pathPos, sideCameraOffset_);
  const Vector3 sideLookPos = ToWorld(pathPos, sideLookOffset_);
  const float sideEase =
      EaseInOutCubic(Progress(timer_, sideMoveStartTime_, sideMoveSec_));
  // 見る点は回り込みの半分の時間でドラゴンへ向けきる
  const float lookEase = EaseOutCubic(
      Progress(timer_, 0.0f, sideMoveStartTime_ + sideMoveSec_ * 0.5f));

  Vector3 cameraPos = Lerp(railCameraPos_, sideCameraPos, sideEase);
  Vector3 lookPos = Lerp(railLookPos_, sideLookPos, lookEase);

  //===========================
  // 上へ引いて見下ろす（引き始めた位置で落ちるのをやめ、ドラゴンだけが雲海へ小さくなる）
  //===========================
  if (timer_ >= pullStartTime_) {
    // 引き始めた時刻のドラゴンの横（回り込みは終わっている）から、そのときのドラゴンの上へ
    const Vector3 pullAnchorPos =
        DragonPositionAt(MotionTimeAt(pullStartTime_));
    const Vector3 pullStartCameraPos =
        ToWorld(pullAnchorPos, sideCameraOffset_);
    const float pullEase =
        EaseOutCubic(Progress(timer_, pullStartTime_, pullSec_));
    cameraPos = Lerp(pullStartCameraPos,
                     ToWorld(pullAnchorPos, pullCameraOffset_), pullEase);
    // 沈みきったあとは、沈みきった所を見たまま止める（雲海の下へ落ち続けるのを追わない）
    const float lookMotionTime = (std::min)(motionTime_, MotionTimeAt(sinkTime_));
    lookPos = DragonPositionAt(lookMotionTime) + kWorldUp * lift_;
  }

  // 被弾の瞬間の揺れ：3つの周波数の sin でずらし、残り時間の2乗で弱める
  if (timer_ < hitShakeSec_) {
    const float remain = 1.0f - timer_ / hitShakeSec_;
    const float power = hitShake_ * remain * remain;
    cameraPos += Vector3{std::sin(timer_ * 53.0f),
                         std::sin(timer_ * 71.0f + 1.3f),
                         std::sin(timer_ * 37.0f + 0.7f)} *
                 power;
  }

  camera_->SetTranslate(cameraPos);
  camera_->SetRotate(LookRotation(cameraPos, lookPos));
  camera_->SetFovY(railCameraFov_);
  camera_->Update();
}

void GameOverSequence::DrawImGui() {
#ifdef USE_IMGUI
  ImGui::Text("Time %.2f / %.2f (motion %.2f, scale %.2f)", timer_,
              card_->GetEndTime(), motionTime_, GetTimeScale());
  // 止めて、つまみで時刻を動かし、その時刻の構図を見る
  // 調整値（文字カードの値は含まない）を JSON に保存・読み込みする
  if (ImGui::Button("Save Game Over Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Game Over Config")) {
    LoadConfig();
  }
  ImGui::Checkbox("Pause", &isPaused_);
  if (isPaused_) {
    ImGui::SliderFloat("Scrub Time", &timer_, 0.0f, card_->GetEndTime());
  }
  ImGui::SeparatorText("Timing");
  ImGui::DragFloat("Slow Sec", &slowSec_, 0.01f, 0.0f, 2.0f);
  ImGui::SliderFloat("Slow Scale", &slowScale_, 0.05f, 1.0f);
  ImGui::DragFloat("Slow Recover Sec", &slowRecoverSec_, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Limp Start", &limpStartTime_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Limp End", &limpEndTime_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Pull Start", &pullStartTime_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Sink Time", &sinkTime_, 0.01f, 0.1f, 8.0f);
  ImGui::DragFloat("Darken Start", &darkenStartTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Darken Sec", &darkenSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Skip Lock Sec", &skipLockSec_, 0.01f, 0.0f, 3.0f);
  ImGui::SeparatorText("Dragon");
  ImGui::DragFloat("Hit Push Distance", &hitPushDistance_, 0.05f, 0.0f, 10.0f);
  ImGui::DragFloat("Hit Lift", &hitLift_, 0.05f, 0.0f, 5.0f);
  ImGui::DragFloat("Hit Push Sec", &hitPushSec_, 0.01f, 0.01f, 2.0f);
  ImGui::DragFloat("Drift Speed", &driftSpeed_, 0.1f, 0.0f, 50.0f);
  ImGui::DragFloat("Drift Decay Sec", &driftDecaySec_, 0.01f, 0.05f, 5.0f);
  ImGui::DragFloat("Fall Accel Sec", &fallAccelSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Sink Depth", &sinkDepth_, 0.1f, 0.0f, 50.0f);
  ImGui::DragFloat("Min Fall Distance", &minFallDistance_, 0.5f, 0.0f, 200.0f);
  ImGui::SliderAngle("Hit Roll", &hitRoll_, -90.0f, 90.0f);
  ImGui::SliderAngle("Limp Roll", &limpRoll_, -90.0f, 90.0f);
  ImGui::SliderAngle("Hit Pitch", &hitPitch_, -90.0f, 90.0f);
  ImGui::SliderAngle("Limp Pitch", &limpPitch_, -90.0f, 90.0f);
  ImGui::SliderAngle("Sink Pitch", &sinkPitch_, -90.0f, 90.0f);
  ImGui::SliderAngle("Struggle Roll", &struggleRoll_, 0.0f, 60.0f);
  ImGui::DragFloat("Lift Speed", &liftSpeed_, 0.1f, 0.0f, 50.0f);
  ImGui::DragFloat("Lift Return Sec", &liftReturnSec_, 0.01f, 0.01f, 3.0f);
  ImGui::SliderAngle("Heave Pitch", &heavePitch_, 0.0f, 45.0f);
  ImGui::Text("lift %.2f / push L %.2f R %.2f", lift_, leftPush_, rightPush_);
  // 一打ちごとに、翼の付け根をどこまで上げ・どこまで下ろしたか（度。正で上）
  for (size_t i = 0; i < UsedBeatCount(); ++i) {
    const WingAngleRange &range = beatAngles_[i];
    if (range.isRecorded) {
      ImGui::Text("Beat %d root L %.0f..%.0f / R %.0f..%.0f",
                  static_cast<int>(i + 1), range.leftMin, range.leftMax,
                  range.rightMin, range.rightMax);
    }
  }
  ImGui::SeparatorText("Wing");
  // 付け根の角度（度。正で上）。一打ちの表から求めた今の値
  ImGui::Text("root L %.0f / R %.0f", WingRootAngle(false, 0.0f),
              WingRootAngle(true, 0.0f));
  ImGui::DragFloat("Hit Rise Sec", &hitRiseSec_, 0.01f, 0.01f, 1.0f);
  ImGui::DragFloat("Hit Up Angle", &hitUpAngle_, 0.5f, -90.0f, 90.0f);
  ImGui::SliderInt("Beat Count", &beatCount_, 1,
                   static_cast<int>(kMaxBeatCount_));
  for (size_t i = 0; i < UsedBeatCount(); ++i) {
    StruggleBeat &beat = beats_[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text("Beat %d", static_cast<int>(i + 1));
    ImGui::DragFloat("Time", &beat.time, 0.01f, 0.0f, 5.0f);
    ImGui::DragFloat2("Down Sec / Angle", &beat.downSec, 0.01f, -90.0f, 90.0f);
    ImGui::DragFloat3("Up Sec / L / R", &beat.upSec, 0.01f, -90.0f, 90.0f);
    ImGui::DragFloat("Right Delay", &beat.rightDelay, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat2("Buckle / Flail", &beat.buckle, 0.01f, 0.0f, 1.0f);
    ImGui::SliderFloat("Lift", &beat.lift, 0.0f, 1.0f);
    ImGui::SliderFloat("Tremble", &beat.tremble, 0.0f, 1.0f);
    ImGui::PopID();
  }
  ImGui::DragFloat("Fold Root Angle", &foldRootAngle_, 0.5f, -90.0f, 90.0f);
  ImGui::DragFloat("Face Up Angle", &faceUpAngle_, 0.5f, -60.0f, 80.0f);
  ImGui::Text("face angle %.0f / face up %.2f / head follow %.2f", faceAngle_,
              gesture_.faceUp, gesture_.headFollow);
  ImGui::SliderFloat("Head Follow", &headFollow_, 0.0f, 1.0f);
  ImGui::SliderFloat("Struggle Face Up Scale", &struggleFaceUpScale_, 0.0f,
                     1.0f);
  ImGui::DragFloat("Hang Start", &hangStartTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Hang Sec", &hangSec_, 0.01f, 0.0f, 3.0f);
  // 脚で掻く強さ・一掻きの長さと、最後の一掻き（時刻は被弾からの秒）
  ImGui::Text("leg paddle %.2f (phase %.2f) / last %.2f %.2f %.2f %.2f",
              gesture_.paddle, gesture_.paddlePhase, gesture_.limpLegSwing[0],
              gesture_.limpLegSwing[1], gesture_.limpLegSwing[2],
              gesture_.limpLegSwing[3]);
  ImGui::DragFloat("Paddle Start", &paddleStartTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Paddle Rise Sec", &paddleRiseSec_, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Paddle Tired Time", &paddleTiredTime_, 0.01f, 0.0f, 3.0f);
  ImGui::SliderFloat("Paddle Tired Strength", &paddleTiredStrength_, 0.0f,
                     1.0f);
  ImGui::DragFloat("Paddle Stop Time", &paddleStopTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat2("Paddle Sec Start / End", &paddleStartSec_, 0.01f, 0.05f,
                    2.0f);
  ImGui::DragFloat("Last Paddle Time", &lastPaddleTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Last Paddle Sec", &lastPaddleSec_, 0.01f, 0.05f, 2.0f);
  ImGui::DragFloat("Last Paddle Leg Delay", &lastPaddleLegDelay_, 0.005f, 0.0f,
                   0.5f);
  ImGui::SliderFloat("Last Paddle Strength", &lastPaddleStrength_, 0.0f, 1.0f);
  // 左前・右前・左後・右後
  ImGui::SliderFloat4("Last Paddle Legs FL/FR/HL/HR",
                      lastPaddleLegWeights_.data(), 0.0f, 1.0f);
  ImGui::SliderFloat("Neck Tremble", &neckTrembleRatio_, 0.0f, 1.0f);
  // 畳み具合と、羽ばたきの姿勢の割合（0: 滑空 〜 1: 体を起こす）
  ImGui::DragFloat("Hit Fold", &hitWing_.fold, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Hit Hover", &hitWing_.hover, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Struggle Fold", &struggleWing_.fold, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Struggle Hover", &struggleWing_.hover, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Limp Fold", &limpWing_.fold, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Limp Hover", &limpWing_.hover, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Struggle Response", &struggleResponse_, 0.1f, 0.0f, 60.0f);
  ImGui::SeparatorText("Camera");
  ImGui::DragFloat("Side Move Start", &sideMoveStartTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Side Move Sec", &sideMoveSec_, 0.01f, 0.05f, 3.0f);
  ImGui::DragFloat3("Side Camera Offset", &sideCameraOffset_.x, 0.05f);
  ImGui::DragFloat3("Side Look Offset", &sideLookOffset_.x, 0.05f);
  ImGui::DragFloat3("Pull Camera Offset", &pullCameraOffset_.x, 0.05f);
  ImGui::DragFloat("Pull Sec", &pullSec_, 0.01f, 0.05f, 3.0f);
  ImGui::DragFloat("Hit Shake", &hitShake_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Hit Shake Sec", &hitShakeSec_, 0.01f, 0.01f, 2.0f);
  ImGui::SeparatorText("Game Over Card");
  card_->DrawImGui();
#endif
}
