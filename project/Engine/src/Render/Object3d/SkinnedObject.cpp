#include "Render/Object3d/SkinnedObject.h"
#include "Core/SrvManager.h"
#include "Render/Model/ModelManager.h"
#include "Render/Renderer/Object3dRenderer.h"
#include <cassert>
#include <cmath>
#include <limits>

void SkinnedObject::Initialize(Object3dRenderer *object3dRenderer,
                               SrvManager *srvManager,
                               const std::string &directoryPath,
                               const std::string &filename) {
  object3dRenderer_ = object3dRenderer;
  srvManager_ = srvManager;

  // モデルデータの読み込み
  ModelManager::GetInstance()->LoadModel(directoryPath + "/" + filename);

  model_ = std::make_unique<Model>();
  model_->Initialize(ModelManager::GetInstance()->GetModelRenderer(),
                     directoryPath, filename);

  // アニメーションの読み込み
  animation_ = LoadAnimationFile(directoryPath, filename);

  // スケルトンの作成
  skeleton_ = CreateSkeleton(model_->GetRootNode());

  // 元の姿勢を控え、回転の上書きは空にしておく
  restTransforms_.clear();
  for (const Joint &joint : skeleton_.joints) {
    restTransforms_.push_back(joint.transform);
  }
  rotationOverrides_.assign(skeleton_.joints.size(), std::nullopt);
  translationOverrides_.assign(skeleton_.joints.size(), std::nullopt);

  // 関節の行列を元の姿勢で計算しておく（最初の Update の前でも関節の行列を読めるように）
  ::Update(skeleton_);

  // スキンクラスターの作成
  skinCluster_ = CreateSkinCluster(object3dRenderer->GetDx12Core(), srvManager,
                                   skeleton_, model_.get());

  // 描画用Object3dの作成と割り当て
  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(object3dRenderer);
  object3d_->SetModel(model_.get());
  object3d_->SetSkinCluster(&skinCluster_);
}

void SkinnedObject::Update(float deltaTime) {
  // 関節を元の姿勢に戻す
  for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
    skeleton_.joints[i].transform = restTransforms_[i];
  }

  if (HasAnimation()) {
    // アニメーション時間の更新
    animationTime_ += deltaTime;
    animationTime_ = std::fmod(animationTime_, animation_.duration);

    // アニメーションの適用（各関節のローカル行列更新）
    ApplyAnimation(skeleton_, animation_, animationTime_);
  }

  // 上書きした回転・位置を反映する
  for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
    if (rotationOverrides_[i]) {
      skeleton_.joints[i].transform.rotate = *rotationOverrides_[i];
    }
    if (translationOverrides_[i]) {
      skeleton_.joints[i].transform.translate = *translationOverrides_[i];
    }
  }

  // スケルトンの更新（親から子への行列伝播）
  ::Update(skeleton_);

  // スキンクラスターの更新（パレットの更新）
  ::Update(skinCluster_, skeleton_);

  // ComputeShaderを実行してスキニング計算
  if (object3dRenderer_ && srvManager_ && model_) {
    auto commandList = object3dRenderer_->GetDx12Core()->GetCommandList();

    // DescriptorHeapのセット
    srvManager_->PreDraw();

    // ResourceBarrier: COMMON or VBV -> UNORDERED_ACCESS
    D3D12_RESOURCE_BARRIER barrierUAV{};
    barrierUAV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierUAV.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrierUAV.Transition.pResource = skinCluster_.skinnedVertexResource.Get();
    barrierUAV.Transition.StateBefore =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER; // 描画後の状態から遷移させる
    barrierUAV.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierUAV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrierUAV);

    commandList->SetComputeRootSignature(
        object3dRenderer_->GetSkinningComputeRootSignature());
    commandList->SetPipelineState(
        object3dRenderer_->GetSkinningComputePipelineState());

    commandList->SetComputeRootDescriptorTable(
        0, skinCluster_.paletteSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(
        1, skinCluster_.inputVertexSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(
        2, skinCluster_.influenceSrvHandle.second);
    commandList->SetComputeRootDescriptorTable(
        3, skinCluster_.skinnedVertexUavHandle.second);
    commandList->SetComputeRootConstantBufferView(
        4, skinCluster_.skinningInformationResource->GetGPUVirtualAddress());

    UINT numVertices =
        static_cast<UINT>(model_->GetModelData().vertices.size());
    commandList->Dispatch((numVertices + 1023) / 1024, 1, 1);

    // ResourceBarrier: UNORDERED_ACCESS -> VBV
    D3D12_RESOURCE_BARRIER barrierVBV{};
    barrierVBV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierVBV.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrierVBV.Transition.pResource = skinCluster_.skinnedVertexResource.Get();
    barrierVBV.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierVBV.Transition.StateAfter =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrierVBV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrierVBV);
  }

  // 3Dオブジェクトの全体更新（WVP行列などの計算）
  object3d_->Update();
}

void SkinnedObject::Draw() {
  if (object3d_) {
    object3d_->Draw();
  }
}

std::optional<int32_t> SkinnedObject::FindJoint(const std::string &name) const {
  auto it = skeleton_.jointMap.find(name);
  if (it == skeleton_.jointMap.end()) {
    return std::nullopt;
  }
  return it->second;
}

const Quaternion &SkinnedObject::GetRestRotation(int32_t jointIndex) const {
  assert(0 <= jointIndex && jointIndex < int32_t(restTransforms_.size()));
  return restTransforms_[jointIndex].rotate;
}

void SkinnedObject::SetJointRotation(int32_t jointIndex,
                                     const Quaternion &rotate) {
  assert(0 <= jointIndex && jointIndex < int32_t(rotationOverrides_.size()));
  rotationOverrides_[jointIndex] = rotate;
}

const Vector3 &SkinnedObject::GetRestTranslation(int32_t jointIndex) const {
  assert(0 <= jointIndex && jointIndex < int32_t(restTransforms_.size()));
  return restTransforms_[jointIndex].translate;
}

void SkinnedObject::SetJointTranslation(int32_t jointIndex,
                                        const Vector3 &translate) {
  assert(0 <= jointIndex &&
         jointIndex < int32_t(translationOverrides_.size()));
  translationOverrides_[jointIndex] = translate;
}

void SkinnedObject::ClearJointOverrides() {
  rotationOverrides_.assign(skeleton_.joints.size(), std::nullopt);
  translationOverrides_.assign(skeleton_.joints.size(), std::nullopt);
}

Matrix4x4 SkinnedObject::GetJointWorldMatrix(int32_t jointIndex) const {
  assert(0 <= jointIndex && jointIndex < int32_t(skeleton_.joints.size()));
  // 関節のモデル空間での行列に、Object3d のワールド行列を掛ける
  return Multiply(skeleton_.joints[jointIndex].skeletonSpaceMatrix,
                  object3d_->GetWorldMatrix());
}

uint32_t SkinnedObject::FindNearestVertex(const Vector3 &modelPosition) const {
  const std::vector<Model::VertexData> &vertices =
      model_->GetModelData().vertices;
  assert(!vertices.empty());
  uint32_t nearest = 0;
  float nearestDistanceSq = (std::numeric_limits<float>::max)();
  for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); ++i) {
    const Vector4 &p = vertices[i].position;
    const float distanceSq =
        LengthSq(Vector3{p.x, p.y, p.z} - modelPosition);
    if (distanceSq < nearestDistanceSq) {
      nearestDistanceSq = distanceSq;
      nearest = i;
    }
  }
  return nearest;
}

void SkinnedObject::ComputeSkinnedVertex(uint32_t vertexIndex,
                                         Vector3 &position,
                                         Vector3 &normal) const {
  const std::vector<Model::VertexData> &vertices =
      model_->GetModelData().vertices;
  assert(vertexIndex < vertices.size());
  const Model::VertexData &vertex = vertices[vertexIndex];
  const VertexInfluence &influence = skinCluster_.mappedInfluence[vertexIndex];
  const Vector3 restPosition = {vertex.position.x, vertex.position.y,
                                vertex.position.z};

  // 影響する関節ごとに（初期姿勢の逆行列 × 今の関節の行列）で動かし、重みで足し合わせる
  Vector3 skinnedPosition{};
  Vector3 skinnedNormal{};
  for (uint32_t i = 0; i < kNumMaxInfluence; ++i) {
    const float weight = influence.weights[i];
    if (weight <= 0.0f) {
      continue;
    }
    const int32_t jointIndex = influence.jointIndices[i];
    const Matrix4x4 skinMatrix =
        Multiply(skinCluster_.inverseBindPoseMatrices[jointIndex],
                 skeleton_.joints[jointIndex].skeletonSpaceMatrix);
    skinnedPosition += TransformPoint(restPosition, skinMatrix) * weight;
    skinnedNormal +=
        TransformNormal(vertex.normal, Transpose(Inverse(skinMatrix))) * weight;
  }

  // モデル空間からワールドへ
  const Matrix4x4 &world = object3d_->GetWorldMatrix();
  position = TransformPoint(skinnedPosition, world);
  normal = Normalize(
      TransformNormal(skinnedNormal, Transpose(Inverse(world))));
}

Matrix4x4 SkinnedObject::ComputeJointModelMatrix(int32_t jointIndex) const {
  assert(0 <= jointIndex && jointIndex < int32_t(skeleton_.joints.size()));
  // 関節から根元へたどり、ローカル行列を右から掛けていく（関節 × 親 × 親の親 …）
  Matrix4x4 result = MakeIdentity4x4();
  std::optional<int32_t> current = jointIndex;
  while (current) {
    const int32_t index = *current;
    const Joint &joint = skeleton_.joints[index];
    // 上書きのない関節は、アニメーションがあれば最後の Update の値、なければ元の姿勢
    const QuaternionTransform &base =
        HasAnimation() ? joint.transform : restTransforms_[index];
    const Quaternion &rotate = rotationOverrides_[index]
                                   ? *rotationOverrides_[index]
                                   : base.rotate;
    const Vector3 &translate = translationOverrides_[index]
                                   ? *translationOverrides_[index]
                                   : base.translate;
    result = Multiply(result, MakeAffineMatrix(base.scale, rotate, translate));
    current = joint.parent;
  }
  return result;
}
