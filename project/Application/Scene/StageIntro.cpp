#include "StageIntro.h"
#include "Camera/GameCamera.h"
#include "Camera/RailCamera.h"
#include "Render/Particle/BillboardParticleEmitter.h"
#include "StageIntroText.h"
#include <algorithm>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
} // namespace

StageIntro::StageIntro() : camera_(std::make_unique<GameCamera>()) {}

StageIntro::~StageIntro() = default;

void StageIntro::Initialize(SpriteRenderer *spriteRenderer) {
  cloudParticles_ = std::make_unique<BillboardParticleEmitter>();
  cloudParticles_->Initialize("resources/cloud_puff.png");
  cloudParticles_->SetBlendMode(IParticleEmitter::BlendMode::Alpha);

  text_ = std::make_unique<StageIntroText>();
  text_->Initialize(spriteRenderer);
}

void StageIntro::Start(const RailCamera &railCamera,
                       const Vector3 &dragonEndPos,
                       const Vector3 &dragonEndRot, float seaHeight) {
  timer_ = 0.0f;
  isFinished_ = false;
  hasBurst_ = false;

  dragonEndPos_ = dragonEndPos;
  dragonEndRot_ = dragonEndRot;

  // レールの右・前を水平にする（雲海の面に対して上下がずれないようにする）
  Vector3 forward = railCamera.GetRailForward();
  railForward_ = Normalize(Vector3{forward.x, 0.0f, forward.z});
  railRight_ = Normalize(Cross(kWorldUp, railForward_));

  // レールカメラの位置・画角と、見ている点
  // ビュー行列の逆行列（カメラのワールド行列）の3行目がカメラの正面方向
  const float kRailLookDistance = 10.0f;
  Matrix4x4 railWorld = Inverse(railCamera.GetViewMatrix());
  Vector3 railLookDir = {railWorld.m[2][0], railWorld.m[2][1],
                         railWorld.m[2][2]};
  railCameraPos_ = railCamera.GetTranslate();
  railLookPos_ = railCameraPos_ + railLookDir * kRailLookDistance;
  railCameraFov_ = railCamera.GetFov();

  // 各ショットの位置
  hoverPos_ = ToWorld(dragonEndPos_, hoverOffset_);
  burstPos_ = {hoverPos_.x, seaHeight, hoverPos_.z};
  riseStartPos_ = burstPos_ - kWorldUp * riseStartDepth_;
  lowCameraPos_ = ToWorld(burstPos_, lowCameraOffset_);
  lowLookPos_ = burstPos_ + kWorldUp * lowLookHeight_;
  revealCameraPos_ = ToWorld(hoverPos_, revealCameraOffset_);
  revealLookPos_ = ToWorld(hoverPos_, revealLookOffset_);

  // 雲の粒を飛ばす向き：S4・S5 のカメラから突き破る点へ向かう水平の向き
  Vector3 away = burstPos_ - lowCameraPos_;
  awayFromCamera_ = Normalize(Vector3{away.x, 0.0f, away.z});

  // 上昇を始める時刻：雲海の面を通る時刻が burstTime_ になるよう逆算する
  // 上昇は EaseOutQuad（進んだ割合 = 1 - (1 - s)^2）なので、
  // 割合 f を進む時間の割合 s は 1 - √(1 - f)
  float riseDistance = hoverPos_.y - riseStartPos_.y;
  float surfaceRatio = std::clamp(riseStartDepth_ / riseDistance, 0.0f, 1.0f);
  float surfaceTimeRatio = 1.0f - std::sqrt(1.0f - surfaceRatio);
  riseStartTime_ = burstTime_ - surfaceTimeRatio * riseSec_;

  dragonPos_ = riseStartPos_;
  dragonRot_ = {risePitch_, dragonEndRot_.y, 0.0f};
  followLookPos_ = lowLookPos_;

  text_->Start();
}

void StageIntro::Skip() {
  if (isFinished_) {
    return;
  }
  dragonPos_ = dragonEndPos_;
  dragonRot_ = dragonEndRot_;
  isFinished_ = true;
  text_->Stop();
}

void StageIntro::Stop() {
  isFinished_ = true;
  text_->Stop();
}

void StageIntro::Update() {
  if (isFinished_) {
    return;
  }

  timer_ += kFrameSec;

  UpdateDragon();
  UpdateCamera();
  EmitCloudParticles();
}

void StageIntro::UpdateDragon() {
  float riseEndTime = riseStartTime_ + riseSec_;

  if (timer_ < riseStartTime_) {
    //===========================
    // 雲海の下で待つ
    //===========================
    dragonPos_ = riseStartPos_;
    dragonRot_ = {risePitch_, dragonEndRot_.y, 0.0f};
  } else if (timer_ < riseEndTime) {
    //===========================
    // S5 上昇：雲海の面を最速で抜け、頂点で止まる（EaseOutQuad）
    // 機首は上昇の終わりの方で水平に戻す
    //===========================
    const float kLevelOffStart = 0.6f; // 上昇の何割から機首を戻し始めるか
    float progress = (timer_ - riseStartTime_) / riseSec_;
    dragonPos_ = Lerp(riseStartPos_, hoverPos_, EaseOutQuad(progress));
    float levelOff = EaseInOutCubic(std::clamp(
        (progress - kLevelOffStart) / (1.0f - kLevelOffStart), 0.0f, 1.0f));
    dragonRot_ = {Lerp(risePitch_, hoverPitch_, levelOff), dragonEndRot_.y,
                  0.0f};
  } else if (timer_ < turnStartTime_) {
    //===========================
    // S6 頂点で止まり、ゆっくり上下しながら体を傾ける
    //===========================
    float bobY = 0.0f;
    float roll = 0.0f;
    CalcHover(timer_ - riseEndTime, bobY, roll);
    dragonPos_ = hoverPos_ + kWorldUp * bobY;
    dragonRot_ = {hoverPitch_, dragonEndRot_.y, roll};
  } else {
    //===========================
    // S7 止まっていた位置から、中間が下がる弧（2次ベジェ曲線）を描いて終了位置へ
    // B(e) = (1-e)^2 * 始点 + 2(1-e)e * 中間 + e^2 * 終点
    //===========================
    float progress = (std::min)((timer_ - turnStartTime_) / turnSec_, 1.0f);
    float ease = EaseInOutCubic(progress);

    float bobY = 0.0f;
    float roll = 0.0f;
    CalcHover(turnStartTime_ - riseEndTime, bobY, roll);
    Vector3 from = hoverPos_ + kWorldUp * bobY;
    Vector3 mid = (from + dragonEndPos_) * 0.5f - kWorldUp * diveDepth_;

    float inv = 1.0f - ease;
    dragonPos_ = from * (inv * inv) + mid * (2.0f * inv * ease) +
                 dragonEndPos_ * (ease * ease);

    // 進む向き：ベジェ曲線の接線 B'(e) = 2(1-e)(中間 - 始点) + 2e(終点 - 中間)
    Vector3 tangent =
        (mid - from) * (2.0f * inv) + (dragonEndPos_ - mid) * (2.0f * ease);
    float horizontal = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
    float pathPitch = std::clamp(std::atan2(-tangent.y, horizontal),
                                 -maxTurnPitch_, maxTurnPitch_);
    // 体の傾き：横（レールの右）へ進む割合に合わせて、進む側の翼を下げる（右へ進むと負）
    float sideRatio = horizontal > 0.0f ? Dot(tangent, railRight_) / horizontal
                                        : 0.0f;
    float bankRoll = -turnBank_ * sideRatio;

    // 機首：止まっていたときの角度 → 進む向き → 終了時の角度
    const float kEnterRatio = 0.3f;  // 進む向きに合わせきるまでの割合
    const float kSettleStart = 0.7f; // 終了時の角度へ戻し始める割合
    float enter = EaseInOutCubic((std::min)(progress / kEnterRatio, 1.0f));
    float settle = EaseInOutCubic(std::clamp(
        (progress - kSettleStart) / (1.0f - kSettleStart), 0.0f, 1.0f));
    float pitch = Lerp(Lerp(hoverPitch_, pathPitch, enter), dragonEndRot_.x,
                       settle);
    float bank = Lerp(Lerp(roll, bankRoll, enter), dragonEndRot_.z, settle);
    dragonRot_ = {pitch, dragonEndRot_.y, bank};

    if (progress >= 1.0f) {
      dragonPos_ = dragonEndPos_;
      dragonRot_ = dragonEndRot_;
      isFinished_ = true;
    }
  }
}

void StageIntro::UpdateCamera() {
  Vector3 cameraPos = lowCameraPos_;
  Vector3 lookPos = lowLookPos_;
  float fov = lowCameraFov_;

  if (timer_ >= burstTime_) {
    //===========================
    // S5 突き破ったあとは、ドラゴンに少し遅れてついて行く点を見る
    // 毎フレーム、差の (1 - e^(-1フレーム/時定数)) だけ近づける
    //===========================
    float follow = 1.0f - std::exp(-kFrameSec / followLagSec_);
    followLookPos_ = Lerp(followLookPos_, dragonPos_, follow);
    lookPos = followLookPos_;
  }

  if (timer_ >= craneStartTime_) {
    //===========================
    // S6 カメラがドラゴンの後ろ上へ上がる
    // 見る点は、カメラが移動の途中まで上がってからドラゴンから前方の空へ移す
    //===========================
    float progress = (std::min)((timer_ - craneStartTime_) / craneSec_, 1.0f);
    cameraPos = Lerp(lowCameraPos_, revealCameraPos_, EaseInOutCubic(progress));
    float lookEase = EaseInOutCubic(std::clamp(
        (progress - craneLookDelay_) / (1.0f - craneLookDelay_), 0.0f, 1.0f));
    lookPos = Lerp(followLookPos_, revealLookPos_, lookEase);
  }

  if (timer_ >= turnStartTime_) {
    //===========================
    // S7 位置・見る点・画角をレールカメラのものへ移す
    // 見る点を補間するので、途中もドラゴンが画面に残る
    //===========================
    float ease = EaseInOutCubic(
        (std::min)((timer_ - turnStartTime_) / turnSec_, 1.0f));
    cameraPos = Lerp(revealCameraPos_, railCameraPos_, ease);
    lookPos = Lerp(revealLookPos_, railLookPos_, ease);
    fov = Lerp(lowCameraFov_, railCameraFov_, ease);
  }

  Vector3 cameraRot = LookRotation(cameraPos, lookPos);

  // 突き破った瞬間の揺れ：3つの周波数のsinでずらし、残り時間の2乗で弱める
  if (timer_ >= burstTime_) {
    float elapsed = timer_ - burstTime_;
    float remain = 1.0f - (std::min)(elapsed / burstShakeSec_, 1.0f);
    float power = burstShake_ * remain * remain;
    cameraPos += Vector3{std::sin(elapsed * 53.0f),
                         std::sin(elapsed * 71.0f + 1.3f),
                         std::sin(elapsed * 37.0f + 0.7f)} *
                 power;
  }

  camera_->SetTranslate(cameraPos);
  camera_->SetRotate(cameraRot);
  camera_->SetFovY(fov);
  camera_->Update();
}

void StageIntro::EmitCloudParticles() {
  const float kTrailSec = 0.3f;     // 突き破ったあと、後ろに雲を引く時間（秒）
  const float kTailDistance = 2.5f; // ドラゴンの位置から雲を引く位置までの距離（下向き）
  const float kFullTurn = 2.0f * std::numbers::pi_v<float>;

  float swellStartTime = burstTime_ - swellLeadSec_;

  // 粒は小さく生んで広げる（不透明度は生まれたときが最大で、寿命に合わせて下がる）
  // 速さには、カメラから遠ざかる向き（awayFromCamera_）を乱れの幅より大きく入れる
  ParticleEmitDesc desc{};
  desc.color = {puffColor_.x, puffColor_.y, puffColor_.z, 1.0f};
  desc.rotateRandom = {0.0f, 0.0f, kFullTurn};

  if (!hasBurst_ && timer_ >= burstTime_) {
    //===========================
    // 突き破った瞬間：雲の粒を奥と上へはね上げる
    //===========================
    const float kAwaySpeed = 4.0f; // カメラから遠ざかる速さ
    const float kUpSpeed = 6.0f;   // 上へ向かう速さ
    hasBurst_ = true;
    desc.position = burstPos_;
    desc.count = 36;
    desc.spawnRandom = {1.5f, 0.2f, 1.5f};
    desc.baseVelocity = awayFromCamera_ * kAwaySpeed + kWorldUp * kUpSpeed;
    desc.velocityRandom = {3.0f, 2.0f, 3.0f};
    desc.lifeMin = 0.5f;
    desc.lifeMax = 1.0f;
    desc.baseScale = {0.4f, 0.4f, 0.4f};
    desc.scaleRandom = {0.5f, 0.5f, 0.5f};
    desc.scaleVelocity = {2.5f, 2.5f, 2.5f};
    cloudParticles_->Emit(desc);
  } else if (timer_ >= swellStartTime && timer_ < burstTime_) {
    //===========================
    // S4 盛り上がり：突き破る点から細い湯気が立ちのぼり、だんだん広く・速くなる
    //===========================
    const float kAwaySpeed = 1.0f; // カメラから遠ざかる速さ
    float swell = (timer_ - swellStartTime) / swellLeadSec_;
    float spread = 0.5f + 0.5f * swell;
    desc.position = burstPos_;
    desc.count = 1;
    desc.spawnRandom = {spread, 0.0f, spread};
    desc.baseVelocity =
        awayFromCamera_ * kAwaySpeed + kWorldUp * (2.0f + 3.0f * swell);
    desc.velocityRandom = {0.5f, 0.8f, 0.5f};
    desc.lifeMin = 0.4f;
    desc.lifeMax = 0.7f;
    desc.baseScale = {0.4f, 0.4f, 0.4f};
    desc.scaleRandom = {0.3f, 0.3f, 0.3f};
    desc.scaleVelocity = {1.2f, 1.2f, 1.2f};
    cloudParticles_->Emit(desc);
  } else if (hasBurst_ && timer_ < burstTime_ + kTrailSec) {
    //===========================
    // 突き破った直後：上昇するドラゴンの後ろ（下側）に雲の粒を引く
    //===========================
    const float kAwaySpeed = 1.5f; // カメラから遠ざかる速さ
    const float kFallSpeed = 3.0f; // 下へ落ちる速さ
    desc.position = dragonPos_ - kWorldUp * kTailDistance;
    desc.count = 2;
    desc.spawnRandom = {0.5f, 0.5f, 0.5f};
    desc.baseVelocity = awayFromCamera_ * kAwaySpeed - kWorldUp * kFallSpeed;
    desc.velocityRandom = {0.8f, 0.8f, 0.8f};
    desc.lifeMin = 0.3f;
    desc.lifeMax = 0.5f;
    desc.baseScale = {0.3f, 0.3f, 0.3f};
    desc.scaleRandom = {0.3f, 0.3f, 0.3f};
    desc.scaleVelocity = {1.0f, 1.0f, 1.0f};
    cloudParticles_->Emit(desc);
  }
}

void StageIntro::UpdateParticles(const Matrix4x4 &viewMatrix,
                                 const Matrix4x4 &projectionMatrix) {
  if (cloudParticles_) {
    cloudParticles_->Update(viewMatrix, projectionMatrix);
  }
}

void StageIntro::DrawParticles() {
  if (cloudParticles_) {
    cloudParticles_->Draw();
  }
}

void StageIntro::UpdateCaption() {
  if (text_) {
    text_->Update();
  }
}

void StageIntro::DrawCaption() {
  if (text_) {
    text_->Draw();
  }
}

const ICamera *StageIntro::GetCamera() const { return camera_.get(); }

DragonWingAnimator::State StageIntro::GetWingState() const {
  if (timer_ < riseStartTime_ + riseSec_) {
    return riseWing_;
  }
  if (timer_ < turnStartTime_) {
    return hoverWing_;
  }
  if (timer_ < turnStartTime_ + turnSec_ * diveWingRatio_) {
    return diveWing_;
  }
  return endWing_;
}

Vector3 StageIntro::ToWorld(const Vector3 &origin, const Vector3 &local) const {
  return origin + railRight_ * local.x + kWorldUp * local.y +
         railForward_ * local.z;
}

void StageIntro::CalcHover(float time, float &bobY, float &roll) const {
  // 止まり始めの揺れを 0 から立ち上げる時間（秒）
  const float kHoverRampSec = 0.3f;
  // 上下の揺れに対する傾きの位相のずれ（ラジアン）
  const float kRollPhaseOffset = 1.0f;

  float ramp = (std::min)(time / kHoverRampSec, 1.0f);
  float phase = 2.0f * std::numbers::pi_v<float> * time / bobPeriod_;
  bobY = bobHeight_ * std::sin(phase) * ramp;
  roll = bobRoll_ * std::sin(phase + kRollPhaseOffset) * ramp;
}

void StageIntro::DrawImGui() {
#ifdef USE_IMGUI
  ImGui::Text("Time %.2f / %.2f", timer_, turnStartTime_ + turnSec_);
  ImGui::SeparatorText("Timing");
  ImGui::DragFloat("Burst Time", &burstTime_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Swell Lead Sec", &swellLeadSec_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Rise Sec", &riseSec_, 0.01f, 0.1f, 5.0f);
  ImGui::DragFloat("Crane Start Time", &craneStartTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Crane Sec", &craneSec_, 0.01f, 0.1f, 5.0f);
  ImGui::SliderFloat("Crane Look Delay", &craneLookDelay_, 0.0f, 0.95f);
  ImGui::DragFloat("Turn Start Time", &turnStartTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Turn Sec", &turnSec_, 0.01f, 0.1f, 5.0f);
  ImGui::SeparatorText("Position");
  ImGui::DragFloat3("Hover Offset", &hoverOffset_.x, 0.1f);
  ImGui::DragFloat("Rise Start Depth", &riseStartDepth_, 0.1f, 0.0f, 30.0f);
  ImGui::DragFloat3("Low Camera Offset", &lowCameraOffset_.x, 0.1f);
  ImGui::DragFloat("Low Look Height", &lowLookHeight_, 0.1f);
  ImGui::DragFloat3("Reveal Camera Offset", &revealCameraOffset_.x, 0.1f);
  ImGui::DragFloat3("Reveal Look Offset", &revealLookOffset_.x, 0.1f);
  ImGui::DragFloat("Dive Depth", &diveDepth_, 0.05f);
  ImGui::SeparatorText("Motion");
  ImGui::SliderFloat("Rise Pitch", &risePitch_, -1.5f, 0.0f);
  ImGui::SliderFloat("Hover Pitch", &hoverPitch_, -0.5f, 0.5f);
  ImGui::SliderFloat("Max Turn Pitch", &maxTurnPitch_, 0.0f, 1.2f);
  ImGui::SliderFloat("Turn Bank", &turnBank_, 0.0f, 1.5f);
  ImGui::DragFloat("Bob Height", &bobHeight_, 0.01f);
  ImGui::DragFloat("Bob Period", &bobPeriod_, 0.05f, 0.1f, 10.0f);
  ImGui::DragFloat("Bob Roll", &bobRoll_, 0.005f);
  ImGui::DragFloat("Follow Lag Sec", &followLagSec_, 0.005f, 0.005f, 1.0f);
  ImGui::DragFloat("Burst Shake", &burstShake_, 0.01f);
  ImGui::DragFloat("Burst Shake Sec", &burstShakeSec_, 0.01f, 0.01f, 2.0f);
  ImGui::SliderFloat("Low Camera Fov", &lowCameraFov_, 0.3f, 1.5f);
  ImGui::SeparatorText("Wing (strength / freq / fold / asym)");
  ImGui::DragFloat4("Rise Wing", &riseWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::DragFloat4("Hover Wing", &hoverWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::DragFloat4("Dive Wing", &diveWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::DragFloat4("End Wing", &endWing_.strength, 0.01f, -1.0f, 4.0f);
  ImGui::SliderFloat("Rise Wing Hover", &riseWing_.hover, 0.0f, 1.0f);
  ImGui::SliderFloat("Hover Wing Hover", &hoverWing_.hover, 0.0f, 1.0f);
  ImGui::SliderFloat("Dive Wing Hover", &diveWing_.hover, 0.0f, 1.0f);
  ImGui::SliderFloat("End Wing Hover", &endWing_.hover, 0.0f, 1.0f);
  ImGui::SliderFloat("Dive Wing Ratio", &diveWingRatio_, 0.0f, 1.0f);
  ImGui::ColorEdit3("Puff Color", &puffColor_.x);
  text_->DrawImGui();
#endif
}
