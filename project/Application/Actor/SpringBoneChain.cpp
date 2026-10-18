#include "SpringBoneChain.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/SkinnedObject.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
// 1/60 秒あたりの値として扱う割合を、実際のフレームの長さに直すための基準のフレーム数
const float kReferenceFrameRate = 60.0f;

// 世界の下向き
const Vector3 kDown = {0.0f, -1.0f, 0.0f};

// 行列の平行移動の成分
Vector3 Translation(const Matrix4x4 &matrix) {
  return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}
} // namespace

void SpringBoneChain::Initialize(SkinnedObject *skinned,
                                 const std::vector<std::string> &jointNames) {
  assert(skinned);
  assert(jointNames.size() >= 2 && "SpringBoneChain: 骨と先端の2つ以上が要る");
  skinned_ = skinned;
  bones_.clear();
  jointIndices_.clear();

  // 骨 i の先端は、次の名前の関節の根元（元の姿勢での、骨 i の空間の位置）
  const Skeleton &skeleton = skinned_->GetSkeleton();
  for (size_t i = 0; i + 1 < jointNames.size(); ++i) {
    const std::optional<int32_t> index = skinned_->FindJoint(jointNames[i]);
    const std::optional<int32_t> tipIndex =
        skinned_->FindJoint(jointNames[i + 1]);
    assert(index && tipIndex && "SpringBoneChain: 関節が見つからない");
    if (!index || !tipIndex) {
      continue;
    }
    Bone bone;
    bone.index = *index;
    bone.parent = skeleton.joints[*index].parent;
    bone.localTip = skinned_->GetRestTranslation(*tipIndex);
    bone.appliedRotation = skinned_->GetRestRotation(*index);
    bones_.push_back(bone);
    jointIndices_.push_back(*index);
  }
  needsReset_ = true;
}

void SpringBoneChain::Update(float deltaTime, const Matrix4x4 &worldMatrix,
                             const Settings &settings) {
  if (!skinned_ || bones_.empty()) {
    return;
  }

  // 止まっている間は揺れを進めず、前の回転をそのまま当てる
  if (deltaTime <= 0.0f && !needsReset_) {
    for (const Bone &bone : bones_) {
      skinned_->SetJointRotation(bone.index, bone.appliedRotation);
    }
    return;
  }

  // 鎖の根元がこのフレームに動いた量。最初のフレームと瞬間移動（モデルの単位で比べる）は動きが分からないので置き直す
  const float worldScale =
      Length({worldMatrix.m[0][0], worldMatrix.m[0][1], worldMatrix.m[0][2]});
  const Vector3 root = Translation(
      Multiply(skinned_->ComputeJointModelMatrix(bones_.front().index),
               worldMatrix));
  Vector3 rootMove = root - previousRoot_;
  const bool isTeleport =
      !hasPreviousRoot_ ||
      (worldScale > 0.0f &&
       Length(rootMove) / worldScale > settings.resetDistance);
  if (isTeleport) {
    rootMove = {};
    needsReset_ = true;
  }
  previousRoot_ = root;
  hasPreviousRoot_ = true;

  // 揺れの減り方を、このフレームの長さに合わせる（60fps のとき drag そのもの）
  const float keep = std::pow(1.0f - std::clamp(settings.drag, 0.0f, 1.0f),
                              deltaTime * kReferenceFrameRate);

  // 根元の骨から順に。先の骨は、手前の骨を揺らした後の位置から計算する
  for (Bone &bone : bones_) {
    // 姿勢どおりの骨（このフレームに書き込まれた姿勢の回転のまま）
    const Matrix4x4 boneWorld =
        Multiply(skinned_->ComputeJointModelMatrix(bone.index), worldMatrix);
    const Vector3 head = Translation(boneWorld);
    const Vector3 poseTip = TransformPoint(bone.localTip, boneWorld);
    const float length = Length(poseTip - head);
    if (length <= 0.0f) {
      continue;
    }
    const Vector3 poseDirection = (poseTip - head) * (1.0f / length);

    // 親のワールド行列（手前の骨はすでに揺らした後）
    const Matrix4x4 parentWorld =
        bone.parent ? Multiply(skinned_->ComputeJointModelMatrix(*bone.parent),
                               worldMatrix)
                    : worldMatrix;
    const Quaternion parentWorldRotation = MakeRotationFromMatrix(parentWorld);
    const Quaternion poseWorldRotation = MakeRotationFromMatrix(boneWorld);

    if (needsReset_) {
      // 姿勢どおりの位置に置き、根元と同じ速さで動いていたことにする。骨は姿勢のまま
      bone.tip = poseTip;
      bone.previousTip = poseTip - rootMove;
      bone.appliedRotation =
          Multiply(Conjugate(parentWorldRotation), poseWorldRotation);
      continue;
    }

    // 先端の点を動かす: 慣性 + 姿勢へ戻すばね + 重さ。
    // 慣性は、根元の前のフレームの動きに、それとの差を keep の割合だけ残して足す。
    // 等速で進むだけなら遅れず、根元が急に加速した分だけ遅れる
    const Vector3 tipMove = bone.tip - bone.previousTip;
    // ばねと重さは骨の長さに比例させ、モデルの大きさで揺れ方が変わらないようにする
    Vector3 nextTip = bone.tip + previousRootMove_ +
                      (tipMove - previousRootMove_) * keep +
                      poseDirection * (settings.stiffness * length * deltaTime) +
                      kDown * (settings.gravity * length * deltaTime);

    // 骨の長さを保ち、姿勢から曲がる角度を抑える
    const Vector3 toTip = nextTip - head;
    const Vector3 direction =
        Length(toTip) > 0.0f ? Normalize(toTip) : poseDirection;
    const Quaternion bend = ClampRotationAngle(
        MakeRotationFromTo(poseDirection, direction), settings.maxAngle);
    nextTip = head + RotateVector(bend, poseDirection) * length;

    bone.previousTip = bone.tip;
    bone.tip = nextTip;

    // ワールド空間の曲げを、骨の回転（親に対する）に直す:
    // 骨の回転 = 親のワールドの回転の逆 × 曲げ × 姿勢どおりの骨のワールドの回転
    bone.appliedRotation = Multiply(Conjugate(parentWorldRotation),
                                    Multiply(bend, poseWorldRotation));
    skinned_->SetJointRotation(bone.index, bone.appliedRotation);
  }
  // 根元の動きが分からないフレームで置き直したときは、次のフレームでも置き直して速さを合わせる
  needsReset_ = isTeleport;
  previousRootMove_ = rootMove;
}
