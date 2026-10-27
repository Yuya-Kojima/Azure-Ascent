#include "GameOverSequence.h"
#include "CaptionCard.h"
#include "Camera/GameCamera.h"
#include "Camera/RailCamera.h"
#include "Core/WindowSystem.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>
#include <numbers>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
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
  // 脚と尾は、もがき始めると重さで垂れていく
  gesture_ = {};
  gesture_.faceUp = EaseOutQuad(Progress(timer_, 0.0f, beats_.front().time));
  gesture_.faceUpAngle = faceUpAngle_;
  gesture_.hang = EaseOutQuad(Progress(timer_, hangStartTime_, hangSec_));

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
  ImGui::Text("face angle %.0f", faceAngle_);
  ImGui::DragFloat("Hang Start", &hangStartTime_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Hang Sec", &hangSec_, 0.01f, 0.0f, 3.0f);
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
