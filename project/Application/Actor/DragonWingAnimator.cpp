#include "DragonWingAnimator.h"
#include "../../externals/nlohmann/json.hpp"
#include "Math/MathUtil.h"
#include "Render/Model/Animation.h"
#include "Render/Model/Model.h"
#include "Render/Object3d/SkinnedObject.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>
#include <string>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
using Config = DragonWingAnimator::Config;

// 調整値の保存先
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/DragonWingConfig.json";

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegToRad = kPi / 180.0f;

// モデル空間の軸（右が +X・上が +Y・頭が +Z）
const Vector3 kLateralAxis = {1.0f, 0.0f, 0.0f};
const Vector3 kVerticalAxis = {0.0f, 1.0f, 0.0f};
const Vector3 kForwardAxis = {0.0f, 0.0f, 1.0f};

// 翼の左右（左の翼は -X 側にある）
const float kLeftSide = -1.0f;
const float kRightSide = 1.0f;

// 関節の名前（tools/Blender/fix_dragon_rig.py で付けたもの）
const char *const kBodyJoint = "Root";
const char *const kNeckJoints[] = {"Neck", "Head_0", "Head_1", "Head_2"};
const char *const kHeadJoint = "Head_3";
// 揺れものの骨の並び（根元から先へ。最後の1つは先端の位置を決めるだけ）
const std::vector<std::string> kTailSpringJoints = {
    "Tail_04", "Tail_05", "Tail_06", "Tail_07", "Tail_08", "Tail_09", "Tail_10"};
const std::vector<std::string> kLegSpringJoints[] = {
    {"FrontLeg_L_1", "FrontLeg_L_2", "FrontLeg_L_3", "FrontLeg_L_4"},
    {"FrontLeg_R_1", "FrontLeg_R_2", "FrontLeg_R_3", "FrontLeg_R_4"},
    {"HindLeg_L_0", "HindLeg_L_1", "HindLeg_L_2", "HindLeg_L_3"},
    {"HindLeg_R_0", "HindLeg_R_1", "HindLeg_R_2", "HindLeg_R_3"},
};
const char *const kTailJoints[] = {"Tail_00", "Tail_01", "Tail_02", "Tail_03",
                                   "Tail_04", "Tail_05", "Tail_06", "Tail_07",
                                   "Tail_08", "Tail_09"};

// 調整値の JSON のキーと、ImGui のスライダーの範囲
struct ConfigItem {
  const char *key;
  float Config::*member;
  float min;
  float max;
};
const ConfigItem kConfigItems[] = {
    {"downstrokeRatio", &Config::downstrokeRatio, 0.1f, 0.9f},
    {"rootAmplitude", &Config::rootAmplitude, 0.0f, 80.0f},
    {"rootBias", &Config::rootBias, -45.0f, 45.0f},
    {"armAmplitude", &Config::armAmplitude, -45.0f, 45.0f},
    {"handAmplitude", &Config::handAmplitude, -45.0f, 45.0f},
    {"fingerAmplitude", &Config::fingerAmplitude, -45.0f, 45.0f},
    {"armLag", &Config::armLag, 0.0f, 0.5f},
    {"handLag", &Config::handLag, 0.0f, 0.5f},
    {"fingerLag", &Config::fingerLag, 0.0f, 0.5f},
    {"fingerJointLag", &Config::fingerJointLag, 0.0f, 0.2f},
    {"upstrokeArmSweep", &Config::upstrokeArmSweep, -60.0f, 60.0f},
    {"upstrokeHandSweep", &Config::upstrokeHandSweep, -90.0f, 90.0f},
    {"upstrokeFingerCurl", &Config::upstrokeFingerCurl, -60.0f, 60.0f},
    {"foldArmSweep", &Config::foldArmSweep, -60.0f, 90.0f},
    {"foldHandSweep", &Config::foldHandSweep, -60.0f, 120.0f},
    {"foldFingerFan", &Config::foldFingerFan, -60.0f, 60.0f},
    {"foldFingerCurl", &Config::foldFingerCurl, -60.0f, 60.0f},
    {"asymmetryGain", &Config::asymmetryGain, 0.0f, 1.0f},
    {"wingTwist", &Config::wingTwist, -30.0f, 30.0f},
    {"tailAmplitude", &Config::tailAmplitude, -10.0f, 10.0f},
    {"tailLag", &Config::tailLag, 0.0f, 0.5f},
    {"tailJointLag", &Config::tailJointLag, 0.0f, 0.2f},
    {"tailTurn", &Config::tailTurn, -10.0f, 10.0f},
    {"bodyBob", &Config::bodyBob, 0.0f, 0.05f},
    {"bodyLag", &Config::bodyLag, 0.0f, 0.5f},
    {"bodyPitchAmplitude", &Config::bodyPitchAmplitude, -15.0f, 15.0f},
    {"bodyPitchLag", &Config::bodyPitchLag, 0.0f, 0.5f},
    {"headLevelPitch", &Config::headLevelPitch, 0.0f, 1.0f},
    {"headLevelRoll", &Config::headLevelRoll, 0.0f, 1.0f},
    {"headTurnLead", &Config::headTurnLead, -30.0f, 30.0f},
    {"headMaxAngle", &Config::headMaxAngle, 0.0f, 90.0f},
    {"neckShare", &Config::neckShare, 0.0f, 1.0f},
    {"headKeepHeight", &Config::headKeepHeight, 0.0f, 1.0f},
    {"neckMaxPitch", &Config::neckMaxPitch, 0.0f, 45.0f},
    {"stateResponse", &Config::stateResponse, 0.1f, 20.0f},
    {"beatAmplitudeJitter", &Config::beatAmplitudeJitter, 0.0f, 0.5f},
    {"beatFrequencyJitter", &Config::beatFrequencyJitter, 0.0f, 0.5f},
    {"beatAsymmetryJitter", &Config::beatAsymmetryJitter, 0.0f, 0.5f},
    {"beatBiasJitter", &Config::beatBiasJitter, 0.0f, 15.0f},
    {"tailSpringStiffness", &Config::tailSpringStiffness, 0.0f, 30.0f},
    {"tailSpringDrag", &Config::tailSpringDrag, 0.0f, 1.0f},
    {"legSpringStiffness", &Config::legSpringStiffness, 0.0f, 30.0f},
    {"legSpringDrag", &Config::legSpringDrag, 0.0f, 1.0f},
    {"springGravity", &Config::springGravity, 0.0f, 10.0f},
    {"tailSpringMaxAngle", &Config::tailSpringMaxAngle, 0.0f, 90.0f},
    {"legSpringMaxAngle", &Config::legSpringMaxAngle, 0.0f, 90.0f},
    {"springResetDistance", &Config::springResetDistance, 0.0f, 5.0f},
};

// 向きベクトルを行列で変換する（平行移動は無視し、長さ 1 に戻す）
Vector3 TransformDirection(const Vector3 &direction, const Matrix4x4 &matrix) {
  return Normalize(TransformNormal(direction, matrix));
}

// 2つの回転を同じとみなす内積の下限（どの飛行姿勢でもこれより近い関節は動かさない）
const float kSameRotationDot = 0.99999f;

// 0〜1 に折り返す
float Wrap01(float value) { return value - std::floor(value); }

// 2つの回転が同じとみなせるか
bool IsSameRotation(const Quaternion &a, const Quaternion &b) {
  const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
  return std::abs(dot) >= kSameRotationDot;
}

// 0 → 1 をなめらかにつなぐ（両端で傾きが 0）
float SmoothStep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// アニメーションの先頭（時刻 0）の回転を、関節の名前で拾う。なければ fallback
Quaternion PoseRotation(const Animation *pose, const std::string &jointName,
                        const Quaternion &fallback) {
  if (!pose) {
    return fallback;
  }
  auto it = pose->nodeAnimations.find(jointName);
  if (it == pose->nodeAnimations.end() || it->second.rotate.keyframes.empty()) {
    return fallback;
  }
  return CalculateValue(it->second.rotate.keyframes, 0.0f);
}

// current を target へ、割合 t だけ近づける
float Approach(float current, float target, float t) {
  return current + (target - current) * t;
}

// 回さない回転
const Quaternion kIdentityQuaternion = {0.0f, 0.0f, 0.0f, 1.0f};

// これより短いベクトルは向きを決められないとみなす
const float kMinDirectionLength = 1.0e-4f;

// モデルの前（+Z）を forward へ、上（+Y）を up へ向ける回転（2つは直交していること）
Quaternion RotationFromAxes(const Vector3 &forward, const Vector3 &up) {
  const Vector3 right = Cross(up, forward);
  Matrix4x4 matrix = MakeIdentity4x4();
  const Vector3 rows[3] = {right, up, forward};
  for (int row = 0; row < 3; ++row) {
    matrix.m[row][0] = rows[row].x;
    matrix.m[row][1] = rows[row].y;
    matrix.m[row][2] = rows[row].z;
  }
  return MakeRotationFromMatrix(matrix);
}

// v から axis の向きの成分を取り除き、長さ 1 にする。短すぎるときは fallback
Vector3 PerpendicularDirection(const Vector3 &v, const Vector3 &axis,
                               const Vector3 &fallback) {
  const Vector3 perpendicular = v - axis * Dot(v, axis);
  if (Length(perpendicular) < kMinDirectionLength) {
    return fallback;
  }
  return Normalize(perpendicular);
}
} // namespace

void DragonWingAnimator::Initialize(SkinnedObject *skinned,
                                    const Animation *glidePose,
                                    const Animation *hoverPose) {
  assert(skinned);
  skinned_ = skinned;
  LoadConfig();
  SetupFlightPoses(glidePose, hoverPose ? hoverPose : glidePose);

  leftWing_ = MakeWing("Wing_L_", kLeftSide);
  rightWing_ = MakeWing("Wing_R_", kRightSide);
  neck_.clear();
  for (const char *name : kNeckJoints) {
    neck_.push_back(MakeJoint(name));
  }
  tail_.clear();
  for (const char *name : kTailJoints) {
    tail_.push_back(MakeJoint(name));
  }
  head_ = MakeJoint(kHeadJoint);
  body_ = MakeJoint(kBodyJoint);

  tailSpring_.Initialize(skinned_, kTailSpringJoints);
  legSpringJoints_.clear();
  for (int leg = 0; leg < kLegCount_; ++leg) {
    legSprings_[leg].Initialize(skinned_, kLegSpringJoints[leg]);
    const std::vector<int32_t> &joints = legSprings_[leg].GetJointIndices();
    legSpringJoints_.insert(legSpringJoints_.end(), joints.begin(),
                            joints.end());
  }
  const Matrix4x4 &restBodyMatrix =
      skinned_->GetSkeleton().joints[body_.index].skeletonSpaceMatrix;
  restBodyRotation_ = MakeRotationFromMatrix(restBodyMatrix);
  restBodyPosition_ = {restBodyMatrix.m[3][0], restBodyMatrix.m[3][1],
                       restBodyMatrix.m[3][2]};

  phase_ = 0.0f;
  currentState_ = targetState_;

  // 1体ごとに別の揺らぎになるよう、種は実行のたびに変える
  random_.seed(std::random_device{}());
  beatTo_.fill(0.0f);
  DrawNextBeat();
}

DragonWingAnimator::DrivenJoint
DragonWingAnimator::MakeJoint(const char *name) const {
  DrivenJoint joint;
  const std::optional<int32_t> index = skinned_->FindJoint(name);
  assert(index && "DragonWingAnimator: 関節が見つからない");
  if (!index) {
    return joint;
  }
  joint.index = *index;
  joint.restTranslation = skinned_->GetRestTranslation(joint.index);

  // 関節の回転は親の空間で表されるので、モデル空間の軸を元の姿勢での親の向きで表し直す
  const Skeleton &skeleton = skinned_->GetSkeleton();
  const std::optional<int32_t> parent = skeleton.joints[joint.index].parent;
  const Matrix4x4 modelToParent =
      parent ? Inverse(skeleton.joints[*parent].skeletonSpaceMatrix)
             : MakeIdentity4x4();
  joint.lateralAxis = TransformDirection(kLateralAxis, modelToParent);
  joint.verticalAxis = TransformDirection(kVerticalAxis, modelToParent);
  joint.forwardAxis = TransformDirection(kForwardAxis, modelToParent);
  return joint;
}

void DragonWingAnimator::SetupFlightPoses(const Animation *glidePose,
                                          const Animation *hoverPose) {
  const std::vector<Joint> &joints = skinned_->GetSkeleton().joints;
  restRotations_.clear();
  glideRotations_.clear();
  hoverRotations_.clear();
  posedJoints_.clear();
  for (const Joint &joint : joints) {
    const Quaternion &rest = skinned_->GetRestRotation(joint.index);
    restRotations_.push_back(rest);
    glideRotations_.push_back(PoseRotation(glidePose, joint.name, rest));
    hoverRotations_.push_back(PoseRotation(hoverPose, joint.name, rest));
    if (!IsSameRotation(rest, glideRotations_.back()) ||
        !IsSameRotation(rest, hoverRotations_.back())) {
      posedJoints_.push_back(joint.index);
    }
  }
}

Quaternion DragonWingAnimator::BaseRotation(int32_t jointIndex) const {
  const Quaternion flight = Slerp(glideRotations_[jointIndex],
                                  hoverRotations_[jointIndex], hoverWeight_);
  return Slerp(restRotations_[jointIndex], flight, currentState_.pose);
}

DragonWingAnimator::Wing
DragonWingAnimator::MakeWing(const char *sidePrefix, float side) const {
  const std::string prefix = sidePrefix;
  Wing wing;
  wing.side = side;
  wing.root = MakeJoint((prefix + "Root").c_str());
  wing.arm = MakeJoint((prefix + "Arm").c_str());
  wing.hand = MakeJoint((prefix + "Hand").c_str());
  for (int finger = 0; finger < kFingerCount_; ++finger) {
    for (int joint = 0; joint < kFingerJointCount_; ++joint) {
      const std::string name = prefix + "Finger" + std::to_string(finger + 1) +
                               "_" + std::to_string(joint);
      wing.fingers[finger][joint] = MakeJoint(name.c_str());
    }
  }
  return wing;
}

void DragonWingAnimator::SetStateImmediate(const State &state) {
  targetState_ = state;
  currentState_ = state;
  // 姿勢が飛ぶので、揺れものも置き直す
  tailSpring_.RequestReset();
  for (SpringBoneChain &leg : legSprings_) {
    leg.RequestReset();
  }
}

void DragonWingAnimator::Update(float deltaTime,
                                const Matrix4x4 &worldMatrix) {
  if (!skinned_) {
    return;
  }

  // 今の状態を、残りの差の一定割合ずつ目標へ近づける
  const float t = 1.0f - std::exp(-config_.stateResponse * deltaTime);
  currentState_.strength =
      Approach(currentState_.strength, targetState_.strength, t);
  currentState_.frequency =
      Approach(currentState_.frequency, targetState_.frequency, t);
  currentState_.fold = Approach(currentState_.fold, targetState_.fold, t);
  currentState_.asymmetry =
      Approach(currentState_.asymmetry, targetState_.asymmetry, t);
  currentState_.pose = Approach(currentState_.pose, targetState_.pose, t);
  currentState_.hover = Approach(currentState_.hover, targetState_.hover, t);

  // 位相は「1秒あたりの回数 × 経過時間」を足していく。1周したら次の1回ぶんの揺らぎを引く
  const float frequency =
      currentState_.frequency *
      (1.0f + config_.beatFrequencyJitter * BeatJitter(kBeatFrequency));
  const float nextPhase = phase_ + frequency * deltaTime;
  phase_ = Wrap01(nextPhase);
  if (nextPhase >= 1.0f) {
    DrawNextBeat();
  }

  // 使う側が決めた羽ばたきの姿勢の割合。畳むほど滑空の姿勢に戻す
  hoverWeight_ = std::clamp(currentState_.hover, 0.0f, 1.0f) *
                 (1.0f - currentState_.fold);

  // 姿勢を先に書き込み、羽ばたきで動かす関節はこのあと上書きする
  UpdatePose();

  // 左右差は、片方を強く・もう片方を弱く打つ
  const float asymmetry =
      (currentState_.asymmetry +
       config_.beatAsymmetryJitter * BeatJitter(kBeatAsymmetry)) *
      config_.asymmetryGain;
  UpdateWing(leftWing_, (std::max)(0.0f, 1.0f + asymmetry));
  UpdateWing(rightWing_, (std::max)(0.0f, 1.0f - asymmetry));
  UpdateTail();

  // 体を羽ばたきで前後に傾けて上下させ、そのあとで頭をその揺れから切り離す
  bodyPitch_ = config_.bodyPitchAmplitude * kDegToRad *
               currentState_.strength * Stroke(config_.bodyPitchLag);
  UpdateBody();
  UpdateHead(worldMatrix);

  // 体と尾の根元が決まってから、尾の先と脚を遅れてしならせる
  UpdateSprings(deltaTime, worldMatrix);
}

void DragonWingAnimator::DrawNextBeat() {
  // 周期の終わりで着いた値から、次の周期を始める
  std::uniform_real_distribution<float> distribution(-1.0f, 1.0f);
  beatFrom_ = beatTo_;
  for (float &value : beatTo_) {
    value = distribution(random_);
  }
}

float DragonWingAnimator::BeatJitter(BeatJitterKind kind) const {
  return Lerp(beatFrom_[kind], beatTo_[kind], SmoothStep(phase_));
}

float DragonWingAnimator::Stroke(float lag) const {
  const float u = Wrap01(phase_ - lag);
  const float down = config_.downstrokeRatio;
  if (u < down) {
    // 打ち下ろし: +1 → -1 を down の割合の時間で
    return std::cos(kPi * u / down);
  }
  // 打ち上げ: -1 → +1 を残りの時間で
  return -std::cos(kPi * (u - down) / (1.0f - down));
}

float DragonWingAnimator::StrokeVelocity(float lag) const {
  const float u = Wrap01(phase_ - lag);
  const float down = config_.downstrokeRatio;
  if (u < down) {
    // 打ち下ろし: 途中で最も速く下へ動く
    return std::sin(kPi * u / down);
  }
  // 打ち上げ: 途中で最も速く上へ動く
  return -std::sin(kPi * (u - down) / (1.0f - down));
}

float DragonWingAnimator::UpstrokeWeight(float lag) const {
  const float u = Wrap01(phase_ - lag);
  const float down = config_.downstrokeRatio;
  if (u < down) {
    return 0.0f;
  }
  return std::sin(kPi * (u - down) / (1.0f - down));
}

void DragonWingAnimator::ApplyRotation(const DrivenJoint &joint,
                                       float aroundLateral,
                                       float aroundVertical,
                                       float aroundForward,
                                       float aroundBone) const {
  if (joint.index < 0) {
    return;
  }
  // Multiply(a, b) は b のあとに a で回す。
  // 先に骨の空間で骨の向きまわりにねじり、混ぜた姿勢のあとに、親の空間で X → Z → Y と回す
  Quaternion rotate = Multiply(
      BaseRotation(joint.index),
      MakeRotateAxisAngleQuaternion(kVerticalAxis, aroundBone));
  rotate = Multiply(
      MakeRotateAxisAngleQuaternion(joint.lateralAxis, aroundLateral), rotate);
  rotate = Multiply(
      MakeRotateAxisAngleQuaternion(joint.forwardAxis, aroundForward), rotate);
  rotate = Multiply(
      MakeRotateAxisAngleQuaternion(joint.verticalAxis, aroundVertical),
      rotate);
  skinned_->SetJointRotation(joint.index, rotate);
}

void DragonWingAnimator::UpdatePose() const {
  for (int32_t index : posedJoints_) {
    skinned_->SetJointRotation(index, BaseRotation(index));
  }
  // 脚の揺れものの骨は前のフレームの揺れが残っているので、姿勢に戻す（尾は UpdateTail で書き直す）
  for (int32_t index : legSpringJoints_) {
    skinned_->SetJointRotation(index, BaseRotation(index));
  }
}

void DragonWingAnimator::UpdateSprings(float deltaTime,
                                       const Matrix4x4 &worldMatrix) {
  SpringBoneChain::Settings settings;
  settings.gravity = config_.springGravity;
  settings.resetDistance = config_.springResetDistance;

  settings.stiffness = config_.tailSpringStiffness;
  settings.drag = config_.tailSpringDrag;
  settings.maxAngle = config_.tailSpringMaxAngle * kDegToRad;
  tailSpring_.Update(deltaTime, worldMatrix, settings);

  settings.stiffness = config_.legSpringStiffness;
  settings.drag = config_.legSpringDrag;
  settings.maxAngle = config_.legSpringMaxAngle * kDegToRad;
  for (SpringBoneChain &leg : legSprings_) {
    leg.Update(deltaTime, worldMatrix, settings);
  }
}

void DragonWingAnimator::UpdateWing(const Wing &wing, float gain) const {
  const float strength =
      currentState_.strength * gain *
      (1.0f + config_.beatAmplitudeJitter * BeatJitter(kBeatAmplitude));
  const float rootBias =
      config_.rootBias + config_.beatBiasJitter * BeatJitter(kBeatBias);
  const float fold = currentState_.fold;

  // 翼を上げるのは前後の軸まわり、後ろへ畳むのは上下の軸まわり。
  // 骨は翼端へ向いているので、骨の向きまわりの正のねじれは、右の翼で前縁が下がり左の翼で上がる。どれも左右で向きが逆になる
  auto apply = [&](const DrivenJoint &joint, float upDegrees,
                   float backDegrees, float twistDegrees = 0.0f) {
    ApplyRotation(joint, 0.0f, wing.side * backDegrees * kDegToRad,
                  wing.side * upDegrees * kDegToRad,
                  wing.side * twistDegrees * kDegToRad);
  };
  // 打ち下ろしの途中ほど前縁を下げるねじれ（腕は半分）。指は手首について回る
  const float kArmTwistRatio = 0.5f;
  auto twist = [&](float lag) {
    return config_.wingTwist * strength * StrokeVelocity(lag);
  };

  // 付け根: 翼全体を上下に打つ
  apply(wing.root,
        rootBias + config_.rootAmplitude * strength * Stroke(0.0f),
        0.0f);

  // 腕・手首: 少し遅れて打ち、打ち上げの途中と畳むときに後ろへ回す
  apply(wing.arm, config_.armAmplitude * strength * Stroke(config_.armLag),
        config_.foldArmSweep * fold +
            config_.upstrokeArmSweep * strength *
                UpstrokeWeight(config_.armLag),
        kArmTwistRatio * twist(config_.armLag));
  apply(wing.hand, config_.handAmplitude * strength * Stroke(config_.handLag),
        config_.foldHandSweep * fold +
            config_.upstrokeHandSweep * strength *
                UpstrokeWeight(config_.handLag),
        twist(config_.handLag));

  // 指: 先の節ほど遅れて打ち、根元より先の節は打ち上げと畳みで下へ曲げる
  for (int finger = 0; finger < kFingerCount_; ++finger) {
    // 翼端側の指ほど大きく後ろへ回す（0: 後ろ寄りの指 〜 1: 翼端の指）
    const float fanWeight = float(finger) / float(kFingerCount_ - 1);
    for (int joint = 0; joint < kFingerJointCount_; ++joint) {
      const float lag = config_.fingerLag + config_.fingerJointLag * joint;
      float up = config_.fingerAmplitude * strength * Stroke(lag);
      float back = 0.0f;
      if (joint == 0) {
        back = config_.foldFingerFan * fold * fanWeight;
      } else {
        up -= config_.foldFingerCurl * fold +
              config_.upstrokeFingerCurl * strength * UpstrokeWeight(lag);
      }
      apply(wing.fingers[finger][joint], up, back);
    }
  }
}

void DragonWingAnimator::UpdateTail() const {
  // 尾は後ろを向いているので、左右の軸まわりの正の回転で上がり、上下の軸まわりの正の回転で左へ曲がる
  for (size_t i = 0; i < tail_.size(); ++i) {
    const float lag = config_.tailLag + config_.tailJointLag * float(i);
    const float up =
        config_.tailAmplitude * currentState_.strength * Stroke(lag);
    const float left = config_.tailTurn * currentState_.asymmetry;
    ApplyRotation(tail_[i], up * kDegToRad, left * kDegToRad, 0.0f);
  }
}

void DragonWingAnimator::UpdateBody() const {
  if (body_.index < 0) {
    return;
  }
  // 打ち下ろしで体が持ち上がる（翼が下にあるほど体は上）
  const float lift =
      -config_.bodyBob * currentState_.strength * Stroke(config_.bodyLag);
  Vector3 translate = body_.restTranslation;
  translate.y += lift;
  skinned_->SetJointTranslation(body_.index, translate);

  // 体の前後の傾き（左右の軸まわりの正の回転で頭が下がる）
  ApplyRotation(body_, bodyPitch_, 0.0f, 0.0f);
}

Quaternion DragonWingAnimator::JointModelRotation(int32_t jointIndex) const {
  return MakeRotationFromMatrix(skinned_->ComputeJointModelMatrix(jointIndex));
}

Vector3 DragonWingAnimator::JointModelPosition(int32_t jointIndex) const {
  const Matrix4x4 matrix = skinned_->ComputeJointModelMatrix(jointIndex);
  return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

void DragonWingAnimator::UpdateHead(const Matrix4x4 &worldMatrix) const {
  if (head_.index < 0 || body_.index < 0) {
    return;
  }
  // Multiply(a, b) は b のあとに a で回す。ここの回転はすべてモデル空間で考える

  // 首と頭蓋を姿勢どおりに戻す（前のフレームの上書きを消す）
  for (const DrivenJoint &joint : neck_) {
    ApplyRotation(joint, 0.0f, 0.0f, 0.0f);
  }
  ApplyRotation(head_, 0.0f, 0.0f, 0.0f);

  // 世界の上向きを、モデル空間で表す
  const Vector3 worldUp =
      TransformDirection(kVerticalAxis, Inverse(worldMatrix));

  //---- 向き ----
  // 体の今の回転 = 羽ばたきの傾き × 飛び方の体の向き。羽ばたきの傾きを外して飛び方の向きを取り出す
  const Quaternion flap =
      MakeRotateAxisAngleQuaternion(kLateralAxis, bodyPitch_);
  const Quaternion body =
      Multiply(Conjugate(flap), JointModelRotation(body_.index));
  // 元の姿勢から体がどれだけ回ったか。モデルの前・上がどこを向いたかが分かる
  const Quaternion bodyTurn = Multiply(body, Conjugate(restBodyRotation_));
  const Vector3 bodyForward = RotateVector(bodyTurn, kForwardAxis);
  const Vector3 bodyUp = RotateVector(bodyTurn, kVerticalAxis);

  // 前向き: 水平に戻した前向きへ headLevelPitch の割合だけ寄せ、曲がる方向へ先に向ける
  const Vector3 levelForward =
      PerpendicularDirection(bodyForward, worldUp, bodyForward);
  Vector3 forward = SafeNormalize(
      Lerp(bodyForward, levelForward, config_.headLevelPitch));
  const float turnLead =
      config_.headTurnLead * kDegToRad * currentState_.asymmetry;
  forward = RotateVector(MakeRotateAxisAngleQuaternion(worldUp, turnLead),
                         forward);

  // 上向き: 前向きに垂直なまま、ロールを除いた上向き（世界の上に最も近い向き）へ headLevelRoll の割合だけ寄せる
  const Vector3 keptUp = PerpendicularDirection(bodyUp, forward, bodyUp);
  const Vector3 levelUp = PerpendicularDirection(worldUp, forward, keptUp);
  const Vector3 up = PerpendicularDirection(
      Lerp(keptUp, levelUp, config_.headLevelRoll), forward, keptUp);

  // 体の飛び方の向き → 頭の目標の向き、への回転。羽ばたきの傾きも打ち消し、首が折れない範囲に抑える
  const Quaternion level =
      Multiply(RotationFromAxes(forward, up), Conjugate(bodyTurn));
  const Quaternion headDelta = ClampRotationAngle(
      Multiply(level, Conjugate(flap)), config_.headMaxAngle * kDegToRad);

  //---- 高さ ----
  // 羽ばたき（体の上下と傾き）がなかったときの頭の位置との差を求める
  const Vector3 headPosition = JointModelPosition(head_.index);
  const Vector3 bodyPosition = JointModelPosition(body_.index);
  const Vector3 headWithoutFlap =
      RotateVector(Conjugate(flap), headPosition - bodyPosition) +
      restBodyPosition_;
  const float headRise = headPosition.y - headWithoutFlap.y;

  // 首の骨を左右の軸まわりに a 回すと、その先の頭は「骨から頭までの前後の距離 × a」だけ下がる。
  // 4本とも同じ a で曲げるので、距離の合計で割る
  float leverSum = 0.0f;
  for (const DrivenJoint &joint : neck_) {
    leverSum += headPosition.z - JointModelPosition(joint.index).z;
  }
  float neckPitch = 0.0f;
  if (leverSum > kMinDirectionLength) {
    const float maxPitch = config_.neckMaxPitch * kDegToRad;
    neckPitch = std::clamp(config_.headKeepHeight * headRise / leverSum,
                           -maxPitch, maxPitch);
  }

  //---- 首と頭蓋へ書き込む ----
  // 首の4本: 高さ合わせの曲げのあとに、向きの差の neckShare の割合を等分して回す
  const Quaternion headTarget =
      Multiply(headDelta, JointModelRotation(head_.index));
  const float shareRatio =
      neck_.empty() ? 0.0f : config_.neckShare / float(neck_.size());
  const Quaternion neckDelta =
      Multiply(Slerp(kIdentityQuaternion, headDelta, shareRatio),
               MakeRotateAxisAngleQuaternion(kLateralAxis, neckPitch));
  const std::vector<Joint> &joints = skinned_->GetSkeleton().joints;
  auto rotateInModelSpace = [&](const DrivenJoint &joint,
                                const Quaternion &modelTarget) {
    // 関節の回転 = 親のモデル空間の回転の逆 × 目標のモデル空間の回転
    const std::optional<int32_t> parent = joints[joint.index].parent;
    const Quaternion parentRotation =
        parent ? JointModelRotation(*parent) : kIdentityQuaternion;
    skinned_->SetJointRotation(
        joint.index, Multiply(Conjugate(parentRotation), modelTarget));
  };
  for (const DrivenJoint &joint : neck_) {
    rotateInModelSpace(joint,
                       Multiply(neckDelta, JointModelRotation(joint.index)));
  }
  // 頭蓋: 首の曲げで残ったずれを、ここでちょうど目標の向きに合わせる
  rotateInModelSpace(head_, headTarget);
}

void DragonWingAnimator::LoadConfig() {
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
  for (const ConfigItem &item : kConfigItems) {
    if (root.contains(item.key) && root[item.key].is_number()) {
      config_.*item.member = root[item.key].get<float>();
    }
  }
}

void DragonWingAnimator::SaveConfig() const {
  nlohmann::json root;
  for (const ConfigItem &item : kConfigItems) {
    root[item.key] = config_.*item.member;
  }
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void DragonWingAnimator::DrawImGui() {
#ifdef USE_IMGUI
  ImGui::Text(
      "current: strength %.2f / freq %.2f / fold %.2f / asym %.2f / pose %.2f "
      "/ hover %.2f",
      currentState_.strength, currentState_.frequency, currentState_.fold,
      currentState_.asymmetry, currentState_.pose, currentState_.hover);
  ImGui::Text("hover weight %.2f (0: glide / 1: hover)", hoverWeight_);
  for (const ConfigItem &item : kConfigItems) {
    ImGui::SliderFloat(item.key, &(config_.*item.member), item.min, item.max);
  }
  if (ImGui::Button("Save Wing Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Wing Config")) {
    LoadConfig();
  }
#endif // USE_IMGUI
}
