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
// 顔の向きを測る骨（頭蓋の先にある。根元から先への線は、鼻先より約 40 度下を向く）
const char *const kFaceBaseJoint = "Head_4";
const char *const kFaceTipJoint = "Head_5";
const char *const kSpineJoints[] = {"Spine_0", "Spine_1", "Spine_2", "Spine_3"};
// 揺れものの骨の並び（根元から先へ。最後の1つは先端の位置を決めるだけ）
const std::vector<std::string> kTailSpringJoints = {
    "Tail_04", "Tail_05", "Tail_06", "Tail_07", "Tail_08", "Tail_09", "Tail_10"};
const std::vector<std::string> kLegSpringJoints[] = {
    {"FrontLeg_L_1", "FrontLeg_L_2", "FrontLeg_L_3", "FrontLeg_L_4"},
    {"FrontLeg_R_1", "FrontLeg_R_2", "FrontLeg_R_3", "FrontLeg_R_4"},
    {"HindLeg_L_0", "HindLeg_L_1", "HindLeg_L_2", "HindLeg_L_3"},
    {"HindLeg_R_0", "HindLeg_R_1", "HindLeg_R_2", "HindLeg_R_3"},
};
// 脚で掻くときに回す骨（kLegSpringJoints と同じ脚の順。付け根と、前脚は肘・後脚は膝）
// どの骨も体の左右の面に並んでいるので、左右の軸まわりに回すと前後に振れる
const char *const kLegPaddleJoints[][2] = {
    {"FrontLeg_L_1", "FrontLeg_L_2"},
    {"FrontLeg_R_1", "FrontLeg_R_2"},
    {"HindLeg_L_0", "HindLeg_L_1"},
    {"HindLeg_R_0", "HindLeg_R_1"},
};
// 前脚の数（kLegPaddleJoints の先頭から）
const int kFrontLegCount = 2;
// 掻く順（左前 → 右後 → 右前 → 左後）での、各脚の順番（kLegPaddleJoints と同じ脚の順）
const int kLegPaddleOrder[] = {0, 2, 3, 1};
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
    {"hangLegStiffness", &Config::hangLegStiffness, 0.0f, 30.0f},
    {"hangSpringGravity", &Config::hangSpringGravity, 0.0f, 40.0f},
    {"hangLegMaxAngle", &Config::hangLegMaxAngle, 0.0f, 90.0f},
    {"hangTailDroop", &Config::hangTailDroop, -10.0f, 20.0f},
    {"windAirDrag", &Config::windAirDrag, 0.0f, 80.0f},
    {"windGust", &Config::windGust, 0.0f, 1.0f},
    {"windGustFrequency", &Config::windGustFrequency, 0.0f, 10.0f},
    {"windWingLift", &Config::windWingLift, -30.0f, 60.0f},
    {"windFlutter", &Config::windFlutter, 0.0f, 20.0f},
    {"windFlutterFrequency", &Config::windFlutterFrequency, 0.0f, 20.0f},
    {"windTailLift", &Config::windTailLift, -10.0f, 20.0f},
    {"windTailSway", &Config::windTailSway, 0.0f, 10.0f},
    {"windNeckSway", &Config::windNeckSway, 0.0f, 10.0f},
    {"windSwayFrequency", &Config::windSwayFrequency, 0.0f, 5.0f},
    {"frontLegPaddle", &Config::frontLegPaddle, 0.0f, 90.0f},
    {"frontElbowBend", &Config::frontElbowBend, -90.0f, 90.0f},
    {"hindLegPaddle", &Config::hindLegPaddle, 0.0f, 90.0f},
    {"hindKneeExtend", &Config::hindKneeExtend, -90.0f, 90.0f},
    {"legPaddleLag", &Config::legPaddleLag, 0.0f, 0.5f},
    {"legBodyRoll", &Config::legBodyRoll, -30.0f, 30.0f},
    {"legSpineCurl", &Config::legSpineCurl, -15.0f, 15.0f},
    {"legSpineSway", &Config::legSpineSway, -15.0f, 15.0f},
    {"trembleFrequency", &Config::trembleFrequency, 0.0f, 30.0f},
    {"neckTremble", &Config::neckTremble, 0.0f, 10.0f},
    {"fingerTremble", &Config::fingerTremble, 0.0f, 10.0f},
    {"buckleArmBend", &Config::buckleArmBend, 0.0f, 90.0f},
    {"buckleHandBend", &Config::buckleHandBend, 0.0f, 90.0f},
    {"buckleLagGain", &Config::buckleLagGain, 0.0f, 5.0f},
    {"flailTailGain", &Config::flailTailGain, 0.0f, 20.0f},
    {"flailBodyPitchGain", &Config::flailBodyPitchGain, 0.0f, 10.0f},
    {"directBeatSec", &Config::directBeatSec, 0.05f, 1.0f},
    {"directPushSpeed", &Config::directPushSpeed, 50.0f, 3000.0f},
};

// 直接モードで、付け根が動く速さを求めるときの時間の差（秒）
const float kDirectVelocitySec = 1.0f / 60.0f;

// 風の揺れの位置のずらし方（IrregularWave の offset、ラジアン）
const float kWindSideOffset = 2.0f;      // 左右の翼の差（片側あたり）
const float kWindTailJointOffset = 0.5f; // 尾の骨 1 本ごとに遅らせる量
const float kWindNeckOffset = 4.0f;      // 首（尾と別の揺れにする）

/// <summary>
/// 向き direction が、上向き up に垂直な面（水平）から上へ向いている角度（度。下向きは負）
/// </summary>
float UpAngle(const Vector3 &direction, const Vector3 &up) {
  const float sine = Dot(SafeNormalize(direction), SafeNormalize(up));
  return std::asin(std::clamp(sine, -1.0f, 1.0f)) / kDegToRad;
}

// 重ねる姿勢の各部の JSON のキー
using GesturePose = DragonWingAnimator::GesturePose;
struct GesturePoseItem {
  const char *key;
  float GesturePose::*member;
};
const GesturePoseItem kGesturePoseItems[] = {
    {"spine", &GesturePose::spine},       {"neck", &GesturePose::neck},
    {"head", &GesturePose::head},         {"tail", &GesturePose::tail},
    {"wingUp", &GesturePose::wingUp},     {"wingBack", &GesturePose::wingBack},
    {"armUp", &GesturePose::armUp},       {"armBack", &GesturePose::armBack},
    {"handUp", &GesturePose::handUp},     {"handBack", &GesturePose::handBack},
    {"fingerFan", &GesturePose::fingerFan},
};
// 重ねる姿勢の ImGui のスライダーの範囲（度）
const float kGesturePoseMinAngle = -90.0f;
const float kGesturePoseMaxAngle = 90.0f;

// 重ねる姿勢の JSON のキーと、Config の中の場所
struct GesturePoseSlot {
  const char *key;
  GesturePose Config::*member;
};
const GesturePoseSlot kGesturePoseSlots[] = {
    {"crouchPose", &Config::crouchPose},
    {"roarPose", &Config::roarPose},
    {"launchPose", &Config::launchPose},
    {"collapsePose", &Config::collapsePose},
    {"lookUpPose", &Config::lookUpPose},
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
  faceBase_ = MakeJoint(kFaceBaseJoint);
  faceTip_ = MakeJoint(kFaceTipJoint);
  spine_.clear();
  for (const char *name : kSpineJoints) {
    spine_.push_back(MakeJoint(name));
  }
  body_ = MakeJoint(kBodyJoint);
  for (int leg = 0; leg < kLegCount_; ++leg) {
    legJoints_[leg].root = MakeJoint(kLegPaddleJoints[leg][0]);
    legJoints_[leg].bend = MakeJoint(kLegPaddleJoints[leg][1]);
  }

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
  lieRotations_.clear();
  lieChestRotations_.clear();
}

void DragonWingAnimator::SetLiePoses(const Animation *liePose,
                                     const Animation *lieChestPose) {
  const std::vector<Joint> &joints = skinned_->GetSkeleton().joints;
  lieRotations_.clear();
  lieChestRotations_.clear();
  for (const Joint &joint : joints) {
    const Quaternion &rest = restRotations_[joint.index];
    lieRotations_.push_back(PoseRotation(liePose, joint.name, rest));
    lieChestRotations_.push_back(PoseRotation(
        lieChestPose ? lieChestPose : liePose, joint.name, rest));
    // 飛行姿勢では動かないが伏せでは動く関節も、毎フレーム姿勢を書き込む
    const bool isPosed =
        std::find(posedJoints_.begin(), posedJoints_.end(), joint.index) !=
        posedJoints_.end();
    if (!isPosed && (!IsSameRotation(rest, lieRotations_.back()) ||
                     !IsSameRotation(rest, lieChestRotations_.back()))) {
      posedJoints_.push_back(joint.index);
    }
  }
}

Quaternion DragonWingAnimator::BaseRotation(int32_t jointIndex) const {
  Quaternion ground = restRotations_[jointIndex];
  if (!lieRotations_.empty() && gesture_.lie > 0.0f) {
    const Quaternion lie =
        Slerp(lieRotations_[jointIndex], lieChestRotations_[jointIndex],
              std::clamp(gesture_.lieChest, 0.0f, 1.0f));
    ground = Slerp(ground, lie, std::clamp(gesture_.lie, 0.0f, 1.0f));
  }
  const Quaternion flight = Slerp(glideRotations_[jointIndex],
                                  hoverRotations_[jointIndex], hoverWeight_);
  return Slerp(ground, flight, currentState_.pose);
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

  // 今の状態を、残りの差の一定割合ずつ目標へ近づける（目標が速さを決めていればそれを使う）
  const float response = targetState_.response > 0.0f
                             ? targetState_.response
                             : config_.stateResponse;
  const float t = 1.0f - std::exp(-response * deltaTime);
  currentState_.strength =
      Approach(currentState_.strength, targetState_.strength, t);
  currentState_.frequency =
      Approach(currentState_.frequency, targetState_.frequency, t);
  currentState_.fold = Approach(currentState_.fold, targetState_.fold, t);
  currentState_.asymmetry =
      Approach(currentState_.asymmetry, targetState_.asymmetry, t);
  currentState_.pose = Approach(currentState_.pose, targetState_.pose, t);
  currentState_.hover = Approach(currentState_.hover, targetState_.hover, t);
  currentState_.sideLag =
      Approach(currentState_.sideLag, targetState_.sideLag, t);
  currentState_.buckle = Approach(currentState_.buckle, targetState_.buckle, t);
  currentState_.flail = Approach(currentState_.flail, targetState_.flail, t);
  currentState_.foldToGlide = targetState_.foldToGlide;

  // 位相は「1秒あたりの回数 × 経過時間」を足していく。1周したら次の1回ぶんの揺らぎを引く
  const float frequency =
      currentState_.frequency *
      (1.0f + config_.beatFrequencyJitter * BeatJitter(kBeatFrequency));
  const float nextPhase = phase_ + frequency * deltaTime;
  phase_ = Wrap01(nextPhase);
  if (nextPhase >= 1.0f) {
    DrawNextBeat();
  }

  // 重ねる姿勢の重みの合計の分だけ、羽ばたきを弱める
  gestureWeight_ = std::clamp(
      gesture_.crouch + gesture_.roar + gesture_.launch + gesture_.collapse +
          gesture_.lookUp,
      0.0f, 1.0f);
  flapScale_ = 1.0f - gestureWeight_;
  trembleTime_ += deltaTime;
  windTime_ += deltaTime;

  // 使う側が決めた羽ばたきの姿勢の割合。畳むほど、foldToGlide の割合で滑空の姿勢に戻す
  hoverWeight_ = std::clamp(currentState_.hover, 0.0f, 1.0f) *
                 (1.0f - currentState_.fold * currentState_.foldToGlide);

  // 姿勢を先に書き込み、羽ばたきで動かす関節はこのあと上書きする
  UpdatePose();

  // 左右差は、片方を強く・もう片方を弱く打つ
  const float asymmetry =
      (currentState_.asymmetry +
       config_.beatAsymmetryJitter * BeatJitter(kBeatAsymmetry)) *
      config_.asymmetryGain;
  // 右の翼は sideLag だけ遅れて打つ（直接モードでは左右の角度を外が決めるので遅らせない）
  const float rightLag = IsDirect() ? 0.0f : currentState_.sideLag;
  leftRootAngle_ =
      UpdateWing(leftWing_, (std::max)(0.0f, 1.0f + asymmetry), 0.0f);
  rightRootAngle_ =
      UpdateWing(rightWing_, (std::max)(0.0f, 1.0f - asymmetry), rightLag);
  UpdateTail();
  UpdateLegReaction();
  UpdateSpine();
  UpdateLegs();

  // 体を羽ばたきで前後に傾けて上下させ、そのあとで頭をその揺れから切り離す
  // 振り回されるほど大きく傾ける
  bodyPitch_ = config_.bodyPitchAmplitude *
               (1.0f + config_.flailBodyPitchGain * currentState_.flail) *
               kDegToRad * StrokeStrength() * flapScale_ *
               MeanStroke(config_.bodyPitchLag);
  UpdateBody();
  faceAngle_ = UpdateHead(worldMatrix);

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

float DragonWingAnimator::GetDownstrokePush(bool isRight) const {
  if (IsDirect()) {
    return (std::max)(WingStrokeVelocity(isRight, 0.0f), 0.0f) * flapScale_;
  }
  const float lag = isRight ? currentState_.sideLag : 0.0f;
  return (std::max)(StrokeVelocity(lag), 0.0f) *
         std::clamp(currentState_.strength, 0.0f, 1.0f) * flapScale_;
}

float DragonWingAnimator::UpstrokeWeight(float lag) const {
  const float u = Wrap01(phase_ - lag);
  const float down = config_.downstrokeRatio;
  if (u < down) {
    return 0.0f;
  }
  return std::sin(kPi * (u - down) / (1.0f - down));
}

float DragonWingAnimator::DirectRootAngle(bool isRight, float lag) const {
  return directRoot_(isRight, lag * config_.directBeatSec);
}

float DragonWingAnimator::WingStroke(bool isRight, float lag) const {
  if (!IsDirect()) {
    return Stroke(lag);
  }
  if (config_.rootAmplitude <= 0.0f) {
    return 0.0f;
  }
  return (DirectRootAngle(isRight, lag) - config_.rootBias) /
         config_.rootAmplitude;
}

float DragonWingAnimator::WingStrokeVelocity(bool isRight, float lag) const {
  if (!IsDirect()) {
    return StrokeVelocity(lag);
  }
  // kDirectVelocitySec 前の角度との差から、下へ動く速さを求め、directPushSpeed で 1 になるように割る
  const float delaySec = lag * config_.directBeatSec;
  const float before = directRoot_(isRight, delaySec + kDirectVelocitySec);
  const float now = directRoot_(isRight, delaySec);
  const float speed = (before - now) / kDirectVelocitySec;
  return std::clamp(speed / config_.directPushSpeed, -1.0f, 1.0f);
}

float DragonWingAnimator::WingUpstrokeWeight(bool isRight, float lag) const {
  if (!IsDirect()) {
    return UpstrokeWeight(lag);
  }
  return (std::max)(-WingStrokeVelocity(isRight, lag), 0.0f);
}

float DragonWingAnimator::MeanStroke(float lag) const {
  if (!IsDirect()) {
    return Stroke(lag);
  }
  return 0.5f * (WingStroke(false, lag) + WingStroke(true, lag));
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
  // 力が抜けるほど、重さを足し、脚のばねを弱めて大きく曲がれるようにする
  // 風を受けるほど、重さで垂れるのをやめ、動く向きの後ろへ押す。押す強さは鎖ごとに位置をずらして揺らがせる
  const float hang = std::clamp(gesture_.hang, 0.0f, 1.0f);
  const float wind = std::clamp(gesture_.wind, 0.0f, 1.0f);
  SpringBoneChain::Settings settings;
  settings.gravity =
      Lerp(config_.springGravity, config_.hangSpringGravity, hang) *
      (1.0f - wind);
  settings.resetDistance = config_.springResetDistance;
  auto airDragOf = [&](float offset) {
    return config_.windAirDrag * wind *
           (1.0f + config_.windGust * IrregularWave(windTime_,
                                                    config_.windGustFrequency,
                                                    offset));
  };

  settings.stiffness = config_.tailSpringStiffness;
  settings.drag = config_.tailSpringDrag;
  settings.maxAngle = config_.tailSpringMaxAngle * kDegToRad;
  settings.airDrag = airDragOf(0.0f);
  tailSpring_.Update(deltaTime, worldMatrix, settings);

  settings.stiffness =
      Lerp(config_.legSpringStiffness, config_.hangLegStiffness, hang);
  settings.drag = config_.legSpringDrag;
  settings.maxAngle =
      Lerp(config_.legSpringMaxAngle, config_.hangLegMaxAngle, hang) *
      kDegToRad;
  for (size_t leg = 0; leg < legSprings_.size(); ++leg) {
    settings.airDrag = airDragOf(float(leg + 1));
    legSprings_[leg].Update(deltaTime, worldMatrix, settings);
  }
}

float DragonWingAnimator::UpdateWing(const Wing &wing, float gain,
                                     float wingLag) const {
  const bool isRight = wing.side > 0.0f;
  // 直接モードでは打つ大きさを付け根の角度が持つので、重ねる姿勢で弱める分だけ掛け、揺らぎも足さない
  const float strength =
      IsDirect()
          ? flapScale_
          : currentState_.strength * gain * flapScale_ *
                (1.0f +
                 config_.beatAmplitudeJitter * BeatJitter(kBeatAmplitude));
  const float rootBias =
      IsDirect()
          ? config_.rootBias
          : config_.rootBias + config_.beatBiasJitter * BeatJitter(kBeatBias);
  const float fold = currentState_.fold;
  const float buckle = currentState_.buckle;
  // 腕より先の遅れ：張りが保てないほど遅れ、この翼だけの遅れを足す
  const float lagScale = 1.0f + config_.buckleLagGain * buckle;
  auto lagOf = [&](float lag) { return lag * lagScale + wingLag; };
  const float armLag = lagOf(config_.armLag);
  const float handLag = lagOf(config_.handLag);
  // 打ち下ろしの途中ほど 1 に近い重み（打ち上げの間は 0）
  auto downstrokeWeight = [&](float lag) {
    return (std::max)(WingStrokeVelocity(isRight, lag), 0.0f);
  };
  auto stroke = [&](float lag) { return WingStroke(isRight, lag); };
  auto upstrokeWeight = [&](float lag) {
    return WingUpstrokeWeight(isRight, lag);
  };

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
    return config_.wingTwist * strength * WingStrokeVelocity(isRight, lag);
  };

  // 風を受けて、付け根は持ち上がり、手首と指は節ごとにずれてはためく（左右でも揺れの位置をずらす）
  const float wind = std::clamp(gesture_.wind, 0.0f, 1.0f);
  auto flutter = [&](float offset) {
    return config_.windFlutter * wind *
           IrregularWave(windTime_, config_.windFlutterFrequency,
                         offset + wing.side * kWindSideOffset);
  };

  // 付け根: 翼全体を上下に打つ（重ねる姿勢の角度を足す）
  const float rootUp = rootBias +
                       config_.rootAmplitude * strength * stroke(wingLag) +
                       GestureAngle(&GesturePose::wingUp) +
                       config_.windWingLift * wind;
  apply(wing.root, rootUp, GestureAngle(&GesturePose::wingBack));

  // 腕・手首: 少し遅れて打ち、打ち上げの途中と畳むときに後ろへ回す
  // 張りが保てないときは、打ち下ろしの途中で下へ折れる
  apply(wing.arm,
        config_.armAmplitude * strength * stroke(armLag) +
            GestureAngle(&GesturePose::armUp) -
            config_.buckleArmBend * buckle * downstrokeWeight(armLag),
        config_.foldArmSweep * fold +
            config_.upstrokeArmSweep * strength * upstrokeWeight(armLag) +
            GestureAngle(&GesturePose::armBack),
        kArmTwistRatio * twist(armLag));
  apply(wing.hand,
        config_.handAmplitude * strength * stroke(handLag) +
            GestureAngle(&GesturePose::handUp) -
            config_.buckleHandBend * buckle * downstrokeWeight(handLag) +
            flutter(0.0f),
        config_.foldHandSweep * fold +
            config_.upstrokeHandSweep * strength * upstrokeWeight(handLag) +
            GestureAngle(&GesturePose::handBack),
        twist(handLag));

  // 指: 先の節ほど遅れて打ち、根元より先の節は打ち上げと畳みで下へ曲げる
  for (int finger = 0; finger < kFingerCount_; ++finger) {
    // 翼端側の指ほど大きく後ろへ回す（0: 後ろ寄りの指 〜 1: 翼端の指）
    const float fanWeight = float(finger) / float(kFingerCount_ - 1);
    // 重ねる姿勢で指を開く向き（-1: 根元側の指を前へ 〜 1: 翼端側の指を後ろへ）
    const float spreadSign = fanWeight * 2.0f - 1.0f;
    for (int joint = 0; joint < kFingerJointCount_; ++joint) {
      const float lag =
          lagOf(config_.fingerLag + config_.fingerJointLag * joint);
      // 震えとはためきは指と節ごとに位相をずらして足す
      const float jointOffset = float(finger * kFingerJointCount_ + joint);
      float up = config_.fingerAmplitude * strength * stroke(lag) +
                 config_.fingerTremble * TrembleWave(jointOffset) +
                 flutter(1.0f + jointOffset);
      float back = 0.0f;
      if (joint == 0) {
        back = config_.foldFingerFan * fold * fanWeight +
               GestureAngle(&GesturePose::fingerFan) * spreadSign;
      } else {
        up -= config_.foldFingerCurl * fold +
              config_.upstrokeFingerCurl * strength * upstrokeWeight(lag);
      }
      apply(wing.fingers[finger][joint], up, back);
    }
  }
  return rootUp;
}

void DragonWingAnimator::UpdateTail() const {
  // 尾は後ろを向いているので、左右の軸まわりの正の回転で上がり、上下の軸まわりの正の回転で左へ曲がる
  // 力が抜けるほど、羽ばたきに合わせて振るのをやめ、根元側を下げる
  // 風を受けるほど、下げるのをやめて根元側を上げ、根元から先へ少しずつ遅れて左右に揺れる
  const float hang = std::clamp(gesture_.hang, 0.0f, 1.0f);
  const float wind = std::clamp(gesture_.wind, 0.0f, 1.0f);
  for (size_t i = 0; i < tail_.size(); ++i) {
    const float lag = config_.tailLag + config_.tailJointLag * float(i);
    // 振り回されるほど大きく振れる
    float up = config_.tailAmplitude *
               (1.0f + config_.flailTailGain * currentState_.flail) *
               StrokeStrength() * flapScale_ * MeanStroke(lag) *
               (1.0f - hang);
    if (i < kGestureTailJointCount_) {
      up += GestureAngle(&GesturePose::tail) -
            config_.hangTailDroop * hang * (1.0f - wind) +
            config_.windTailLift * wind;
    }
    const float left =
        config_.tailTurn * currentState_.asymmetry +
        config_.windTailSway * wind *
            IrregularWave(windTime_, config_.windSwayFrequency,
                          -kWindTailJointOffset * float(i));
    ApplyRotation(tail_[i], up * kDegToRad, left * kDegToRad, 0.0f);
  }
}

void DragonWingAnimator::UpdateSpine() const {
  // 背骨を1本ずつ同じ角度で曲げる。脚の反動で、対角の差の分だけ丸め、左右の差の分だけ左右にくねらせる
  const float curl =
      GestureAngle(&GesturePose::spine) + config_.legSpineCurl * legDiagonalDiff_;
  const float sway = config_.legSpineSway * legSideDiff_;
  for (const DrivenJoint &joint : spine_) {
    ApplyRotation(joint, curl * kDegToRad, sway * kDegToRad, 0.0f);
  }
}

float DragonWingAnimator::PaddleCycle(int leg) const {
  return 2.0f * kPi *
         Wrap01(gesture_.paddlePhase -
                config_.legPaddleLag * float(kLegPaddleOrder[leg]));
}

float DragonWingAnimator::PaddlePosition(int leg) const {
  // 周期の始めに前の端（-1）にあり、前半で後ろの端（+1）へ振って、後半で前へ戻す
  const float paddle = std::clamp(gesture_.paddle, 0.0f, 1.0f);
  return -paddle * std::cos(PaddleCycle(leg));
}

void DragonWingAnimator::UpdateLegReaction() {
  // 脚の順は左前・右前・左後・右後
  const float frontLeft = PaddlePosition(0);
  const float frontRight = PaddlePosition(1);
  const float hindLeft = PaddlePosition(2);
  const float hindRight = PaddlePosition(3);
  legSideDiff_ = 0.5f * (frontLeft + hindLeft) - 0.5f * (frontRight + hindRight);
  legDiagonalDiff_ =
      0.5f * (frontLeft + hindRight) - 0.5f * (frontRight + hindLeft);
  bodyRoll_ = config_.legBodyRoll * kDegToRad * legSideDiff_;
}

void DragonWingAnimator::UpdateLegs() const {
  // 脚の骨は左右の軸まわりの正の回転で、骨の先が後ろへ動く（前脚の肘は曲がり、後脚の膝は伸びる向きが負）
  // 掻く周期の前半で前の端から後ろの端へ振り（前脚は手前へ引き、後脚は後ろへ蹴る）、後半で前へ戻す
  const float paddle = std::clamp(gesture_.paddle, 0.0f, 1.0f);
  for (int leg = 0; leg < kLegCount_; ++leg) {
    const bool isFront = leg < kFrontLegCount;
    const float amplitude =
        isFront ? config_.frontLegPaddle : config_.hindLegPaddle;
    // 振る途中で、前脚は肘を曲げ、後脚は膝を伸ばす（どちらも負の回転）
    const float bendAmount =
        isFront ? -config_.frontElbowBend : -config_.hindKneeExtend;

    // 前の端（-amplitude）から後ろの端（+amplitude）へ振る間ほど 1 に近い重み（前へ戻す間は 0）
    const float pushWeight = (std::max)(std::sin(PaddleCycle(leg)), 0.0f);
    const float swing = amplitude * PaddlePosition(leg);
    const float bend = bendAmount * paddle * pushWeight;

    ApplyRotation(legJoints_[leg].root, swing * kDegToRad, 0.0f, 0.0f);
    ApplyRotation(legJoints_[leg].bend, bend * kDegToRad, 0.0f, 0.0f);
  }
}

float DragonWingAnimator::GestureAngle(float GesturePose::*part) const {
  return gesture_.crouch * (config_.crouchPose.*part) +
         gesture_.roar * (config_.roarPose.*part) +
         gesture_.launch * (config_.launchPose.*part) +
         gesture_.collapse * (config_.collapsePose.*part) +
         gesture_.lookUp * (config_.lookUpPose.*part);
}

float DragonWingAnimator::TrembleWave(float phaseOffset) const {
  // 骨ごとに位相をずらす量（ラジアン）
  const float kPhaseStep = 1.7f;
  const float phase = 2.0f * kPi * config_.trembleFrequency * trembleTime_ +
                      kPhaseStep * phaseOffset;
  return gesture_.tremble * std::sin(phase);
}

void DragonWingAnimator::UpdateBody() const {
  if (body_.index < 0) {
    return;
  }
  // 打ち下ろしで体が持ち上がる（翼が下にあるほど体は上）
  const float lift = -config_.bodyBob * StrokeStrength() * flapScale_ *
                     MeanStroke(config_.bodyLag);
  Vector3 translate = body_.restTranslation;
  translate.y += lift;
  skinned_->SetJointTranslation(body_.index, translate);

  // 体の前後の傾き（左右の軸まわりの正の回転で頭が下がる）と、脚の反動での左右の傾き
  ApplyRotation(body_, bodyPitch_, 0.0f, bodyRoll_);
}

Quaternion DragonWingAnimator::JointModelRotation(int32_t jointIndex) const {
  return MakeRotationFromMatrix(skinned_->ComputeJointModelMatrix(jointIndex));
}

Vector3 DragonWingAnimator::JointModelPosition(int32_t jointIndex) const {
  const Matrix4x4 matrix = skinned_->ComputeJointModelMatrix(jointIndex);
  return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}

float DragonWingAnimator::UpdateHead(const Matrix4x4 &worldMatrix) const {
  if (head_.index < 0 || body_.index < 0) {
    return 0.0f;
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
  // 体の今の回転 = 揺れ（脚の反動の左右の傾き × 羽ばたきの前後の傾き）× 飛び方の体の向き。揺れを外して飛び方の向きを取り出す
  const Quaternion flap =
      Multiply(MakeRotateAxisAngleQuaternion(kForwardAxis, bodyRoll_),
               MakeRotateAxisAngleQuaternion(kLateralAxis, bodyPitch_));
  const Quaternion body =
      Multiply(Conjugate(flap), JointModelRotation(body_.index));
  // 元の姿勢から体がどれだけ回ったか。モデルの前・上がどこを向いたかが分かる
  const Quaternion bodyTurn = Multiply(body, Conjugate(restBodyRotation_));
  const Vector3 bodyForward = RotateVector(bodyTurn, kForwardAxis);
  const Vector3 bodyUp = RotateVector(bodyTurn, kVerticalAxis);

  // 前向き: 水平に戻した前向きへ headLevelPitch の割合だけ寄せ、曲がる方向へ先に向ける
  const Vector3 levelForward =
      PerpendicularDirection(bodyForward, worldUp, bodyForward);
  // 重ねる姿勢の重みの分だけ水平に戻すのを弱め、体を起こした向きを頭に伝える
  const float levelPitch = config_.headLevelPitch * flapScale_;
  Vector3 forward =
      SafeNormalize(Lerp(bodyForward, levelForward, levelPitch));
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
  // 体についていかせる割合の分だけ、目標の向きへの回転を弱める
  const float headFollow = std::clamp(gesture_.headFollow, 0.0f, 1.0f);
  const Quaternion headDelta =
      Slerp(ClampRotationAngle(Multiply(level, Conjugate(flap)),
                               config_.headMaxAngle * kDegToRad),
            kIdentityQuaternion, headFollow);

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
    neckPitch = std::clamp(config_.headKeepHeight * (1.0f - headFollow) *
                               headRise / leverSum,
                           -maxPitch, maxPitch);
  }

  //---- 首と頭蓋へ書き込む ----
  // 首の4本: 高さ合わせの曲げのあとに、向きの差の neckShare の割合を等分して回す
  // 重ねる姿勢の首の角度（震えと、首を下げる角度の等分を含む、度）。首の4本を同じ角度で曲げ、頭蓋はその合計に自分の角度を足して回す
  // 風を受けると、首は尾とは別の位置の揺れで上下に振られる
  const float neckGestureAngle =
      GestureAngle(&GesturePose::neck) +
      config_.neckTremble * gesture_.neckTrembleRatio * TrembleWave(0.0f) +
      (neck_.empty() ? 0.0f : gesture_.headDrop / float(neck_.size())) +
      config_.windNeckSway * std::clamp(gesture_.wind, 0.0f, 1.0f) *
          IrregularWave(windTime_, config_.windSwayFrequency,
                        kWindNeckOffset);
  float headGestureAngle = GestureAngle(&GesturePose::head);
  // 姿勢どおりの頭蓋の回転（頭蓋の目標の向きは、これに向きの差と重ねる姿勢を足して決める）
  const Quaternion headPose = JointModelRotation(head_.index);
  auto headTargetFor = [&](float neckAngle) {
    const float headAngle =
        (neckAngle * float(neck_.size()) + headGestureAngle) * kDegToRad;
    return Multiply(MakeRotateAxisAngleQuaternion(kLateralAxis, headAngle),
                    Multiply(headDelta, headPose));
  };

  // 顔の向きを世界で決める: 頭蓋がこのまま向く顔の向きを求め、faceUpAngle になるまでの差を頭蓋だけで回す（首は曲げない）
  // 頭蓋を左右の軸まわりに回した分だけ、頭蓋の先の顔も同じ向きに回る
  const bool hasFace = faceBase_.index >= 0 && faceTip_.index >= 0;
  if (hasFace && gesture_.faceUp > 0.0f) {
    // 頭蓋から見た顔の向き（頭蓋の先の骨の根元から先へ）
    const Vector3 faceInHead = RotateVector(
        Conjugate(headPose), JointModelPosition(faceTip_.index) -
                                 JointModelPosition(faceBase_.index));
    const Vector3 face =
        RotateVector(headTargetFor(neckGestureAngle), faceInHead);
    const float faceAngle = UpAngle(face, worldUp);
    // 頭蓋の角度は正で鼻先が下がるので、上げたい角度の分だけ引く
    headGestureAngle -= gesture_.faceUp * (gesture_.faceUpAngle - faceAngle);
  }
  const float gestureNeck = neckGestureAngle * kDegToRad;
  const Quaternion headTarget = headTargetFor(neckGestureAngle);
  const float shareRatio =
      neck_.empty() ? 0.0f : config_.neckShare / float(neck_.size());
  const Quaternion neckDelta = Multiply(
      Slerp(kIdentityQuaternion, headDelta, shareRatio),
      MakeRotateAxisAngleQuaternion(kLateralAxis, neckPitch + gestureNeck));
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

  // 書き込んだあとの顔の向きを測る
  if (!hasFace) {
    return 0.0f;
  }
  return UpAngle(JointModelPosition(faceTip_.index) -
                     JointModelPosition(faceBase_.index),
                 worldUp);
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
  for (const GesturePoseSlot &slot : kGesturePoseSlots) {
    if (!root.contains(slot.key) || !root[slot.key].is_object()) {
      continue;
    }
    const nlohmann::json &pose = root[slot.key];
    for (const GesturePoseItem &item : kGesturePoseItems) {
      if (pose.contains(item.key) && pose[item.key].is_number()) {
        (config_.*slot.member).*item.member = pose[item.key].get<float>();
      }
    }
  }
}

void DragonWingAnimator::SaveConfig() const {
  nlohmann::json root;
  for (const ConfigItem &item : kConfigItems) {
    root[item.key] = config_.*item.member;
  }
  for (const GesturePoseSlot &slot : kGesturePoseSlots) {
    for (const GesturePoseItem &item : kGesturePoseItems) {
      root[slot.key][item.key] = (config_.*slot.member).*item.member;
    }
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
  ImGui::Text("direct %s / root L %.0f R %.0f", IsDirect() ? "on" : "off",
              leftRootAngle_, rightRootAngle_);
  for (const ConfigItem &item : kConfigItems) {
    ImGui::SliderFloat(item.key, &(config_.*item.member), item.min, item.max);
  }
  ImGui::Text("gesture: crouch %.2f / roar %.2f / launch %.2f / tremble %.2f",
              gesture_.crouch, gesture_.roar, gesture_.launch,
              gesture_.tremble);
  ImGui::Text("gesture: collapse %.2f / lookUp %.2f / hang %.2f / faceUp %.2f",
              gesture_.collapse, gesture_.lookUp, gesture_.hang,
              gesture_.faceUp);
  ImGui::Text("gesture: paddle %.2f (phase %.2f)", gesture_.paddle,
              gesture_.paddlePhase);
  ImGui::Text("leg reaction: side %.2f / diagonal %.2f / body roll %.1f / "
              "head follow %.2f / head drop %.1f",
              legSideDiff_, legDiagonalDiff_, bodyRoll_ / kDegToRad,
              gesture_.headFollow, gesture_.headDrop);
  for (const GesturePoseSlot &slot : kGesturePoseSlots) {
    if (ImGui::TreeNode(slot.key)) {
      for (const GesturePoseItem &item : kGesturePoseItems) {
        ImGui::SliderFloat(item.key, &((config_.*slot.member).*item.member),
                           kGesturePoseMinAngle, kGesturePoseMaxAngle);
      }
      ImGui::TreePop();
    }
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
