#include "DragonModel.h"
#include "Math/MathUtil.h"
#include "Render/Model/Model.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Object3d/SkinnedObject.h"
#include <cassert>

namespace {
// モデルの場所（tools/Blender/fix_dragon_rig.py で書き出したもの）
const char *const kModelDirectory = "resources/player_dragon";
const char *const kModelFile = "player_dragon.gltf";
// 飛行姿勢（骨だけのファイル。先頭の姿勢を使う）
const char *const kGlidePoseFile = "player_dragon_glide_pose.gltf";
const char *const kHoverPoseFile = "player_dragon_hover_pose.gltf";

// 翼端の指の最後の骨
const char *const kLeftTipJoint = "Wing_L_Finger3_2";
const char *const kRightTipJoint = "Wing_R_Finger3_2";
// 翼端の骨の長さ（モデルの単位、Blender で計測）。骨の根元から骨の向き（ローカル +Y）にこの長さ進んだ点が翼端
const float kTipBoneLength = 0.126f;
// 頭の骨
const char *const kHeadJoint = "Head_3";

// 鏡面反射の倍率（1 で Object3d の標準の強さ）
const float kSpecularScale = 0.3f;
} // namespace

DragonModel::DragonModel() = default;
DragonModel::~DragonModel() = default;

void DragonModel::Initialize(Object3dRenderer *object3dRenderer,
                             SrvManager *srvManager) {
  skinned_ = std::make_unique<SkinnedObject>();
  skinned_->Initialize(object3dRenderer, srvManager, kModelDirectory,
                       kModelFile);
  skinned_->GetObject3d()->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
  skinned_->GetObject3d()->SetSpecularScale(kSpecularScale);

  const Animation glidePose = LoadAnimationFile(kModelDirectory, kGlidePoseFile);
  const Animation hoverPose = LoadAnimationFile(kModelDirectory, kHoverPoseFile);
  wing_.Initialize(skinned_.get(), &glidePose, &hoverPose);

  leftTipJoint_ = skinned_->FindJoint(kLeftTipJoint).value_or(-1);
  rightTipJoint_ = skinned_->FindJoint(kRightTipJoint).value_or(-1);
  headJoint_ = skinned_->FindJoint(kHeadJoint).value_or(-1);
  assert(leftTipJoint_ >= 0 && rightTipJoint_ >= 0 && headJoint_ >= 0);
}

void DragonModel::SetWingState(const DragonWingAnimator::State &state) {
  wing_.SetTargetState(state);
}

void DragonModel::SetWingStateImmediate(
    const DragonWingAnimator::State &state) {
  wing_.SetStateImmediate(state);
}

void DragonModel::Update(float deltaTime) {
  wing_.Update(deltaTime, ComputeWorldMatrix());
  skinned_->Update(deltaTime);
}

Matrix4x4 DragonModel::ComputeWorldMatrix() const {
  const Object3d *object = skinned_->GetObject3d();
  Matrix4x4 world = MakeAffineMatrix(
      object->GetScale(), object->GetRotation(), object->GetTranslation());
  if (const Object3d *parent = object->GetParent()) {
    world = Multiply(world, parent->GetWorldMatrix());
  }
  return world;
}

void DragonModel::Draw() { skinned_->Draw(); }

Object3d *DragonModel::GetObject3d() const { return skinned_->GetObject3d(); }

Vector3 DragonModel::GetLeftWingTip() const {
  return GetWingTip(leftTipJoint_);
}

Vector3 DragonModel::GetRightWingTip() const {
  return GetWingTip(rightTipJoint_);
}

Vector3 DragonModel::GetHeadPosition() const {
  if (headJoint_ < 0) {
    return {};
  }
  return TransformPoint({0.0f, 0.0f, 0.0f},
                        skinned_->GetJointWorldMatrix(headJoint_));
}

Vector3 DragonModel::GetWingTip(int32_t jointIndex) const {
  if (jointIndex < 0) {
    return {};
  }
  return TransformPoint({0.0f, kTipBoneLength, 0.0f},
                        skinned_->GetJointWorldMatrix(jointIndex));
}
