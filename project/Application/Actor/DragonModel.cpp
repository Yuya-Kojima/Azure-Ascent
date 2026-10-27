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
// 伏せの姿勢（骨だけのファイル。先頭の姿勢を使う）
const char *const kLiePoseFile = "player_dragon_lie_pose.gltf";
const char *const kLieChestPoseFile = "player_dragon_lie_chest_pose.gltf";

// 翼端の指の最後の骨
const char *const kLeftTipJoint = "Wing_L_Finger3_2";
const char *const kRightTipJoint = "Wing_R_Finger3_2";
// 翼端の骨の長さ（モデルの単位、Blender で計測）。骨の根元から骨の向き（ローカル +Y）にこの長さ進んだ点が翼端
const float kTipBoneLength = 0.126f;
// 頭の骨
const char *const kHeadJoint = "Head_3";

// 鏡面反射の倍率（1 で Object3d の標準の強さ）
const float kSpecularScale = 0.3f;

// 傷の中心にする、右の胸〜脇腹（翼の付け根の下、前脚の後ろ）の表面の点（モデル空間。+X が右、+Z が前）
const Vector3 kWoundModelPosition = {0.05f, 0.21f, 0.17f};
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
  const Animation liePose = LoadAnimationFile(kModelDirectory, kLiePoseFile);
  const Animation lieChestPose =
      LoadAnimationFile(kModelDirectory, kLieChestPoseFile);
  wing_.SetLiePoses(&liePose, &lieChestPose);

  leftTipJoint_ = skinned_->FindJoint(kLeftTipJoint).value_or(-1);
  rightTipJoint_ = skinned_->FindJoint(kRightTipJoint).value_or(-1);
  headJoint_ = skinned_->FindJoint(kHeadJoint).value_or(-1);
  assert(leftTipJoint_ >= 0 && rightTipJoint_ >= 0 && headJoint_ >= 0);

  woundVertex_ = skinned_->FindNearestVertex(kWoundModelPosition);
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
  UpdateGlow();
}

void DragonModel::UpdateGlow() {
  Object3d *object = skinned_->GetObject3d();
  object->SetPatternGlow(glow_.patternColor, glow_.pattern, glow_.patternLow,
                         glow_.patternHigh);

  //===========================
  // 傷の場所：体の表面の頂点を、スキニングと同じ計算で動かして追いかける
  // 面に沿った向きは、体の前の向きから外向きの成分を除いたもの
  //===========================
  skinned_->ComputeSkinnedVertex(woundVertex_, woundFrame_.center,
                                 woundFrame_.normal);
  const Matrix4x4 &world = object->GetWorldMatrix();
  const Vector3 bodyForward = {world.m[2][0], world.m[2][1], world.m[2][2]};
  woundFrame_.scale =
      Length(Vector3{world.m[0][0], world.m[0][1], world.m[0][2]});
  woundFrame_.tangent =
      Normalize(bodyForward -
                woundFrame_.normal * Dot(bodyForward, woundFrame_.normal));

  Object3d::Wound wound;
  wound.center = woundFrame_.center;
  wound.normal = woundFrame_.normal;
  wound.tangent = woundFrame_.tangent;
  wound.radius = glow_.woundRadius * woundFrame_.scale;
  wound.crackLength = glow_.crackLength * woundFrame_.scale;
  wound.crackWidth = glow_.crackWidth * woundFrame_.scale;
  wound.rimWidth = glow_.rimWidth;
  wound.rimJag = glow_.rimJag;
  wound.open = glow_.woundOpen;
  wound.glowColor = glow_.woundColor;
  wound.glow = glow_.woundGlow;
  wound.spill = glow_.spill;
  object->SetWound(wound);
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
