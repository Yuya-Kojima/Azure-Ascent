#include "Object3d/Object3d.h"
#include "Camera/ICamera.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Model/ModelManager.h"
#include "Renderer/Object3dRenderer.h"
#include "Texture/TextureManager.h"
#include <cassert>
#include <fstream>
#include <numbers>

void Object3d::Initialize(Object3dRenderer *object3dRenderer) {

  if (!object3dRenderer) {
    Logger::Log("[Object3d] Initialize: object3dRenderer is null\n");
    assert(false && "Object3d::Initialize: object3dRenderer is null");
    return;
  }

  object3dRenderer_ = object3dRenderer;

  dx12Core_ = object3dRenderer->GetDx12Core();

  if (!dx12Core_) {
    Logger::Log("[Object3d] Initialize: dx12Core is null\n");
    assert(false && "Object3d::Initialize: dx12Core is null");
    return;
  }

  CreateTransformationMatrixData();

  CreateCameraForGPUData();

  CreateMaterialData();

  TextureManager::GetInstance()->LoadTexture("resources/Skybox/Skybox.dds");
  TextureManager::GetInstance()->LoadTexture(maskTexturePath_);

  this->camera_ = nullptr;
}

void Object3d::Update() {

  assert(object3dRenderer_ && "Object3d::Update: not initialized");
  assert(transformationMatrixData &&
         "Object3d::Update: transformationMatrixData is null");
  assert(cameraForGPUData && "Object3d::Update: cameraForGPUData is null");

  const ICamera *activeCamera =
      (camera_ != nullptr) ? camera_ : object3dRenderer_->GetDefaultCamera();

  Matrix4x4 localMatrix = MakeIdentity4x4();
  if (isPlayingAnimation_ && model_) {
      animationTime_ += 1.0f / 60.0f; // 時刻を進める
      animationTime_ = std::fmod(animationTime_, currentAnimation_.duration); // リピート再生
      NodeAnimation& rootNodeAnimation = currentAnimation_.nodeAnimations[model_->GetRootNode().name];
      Vector3 translate = CalculateValue(rootNodeAnimation.translate.keyframes, animationTime_);
      Quaternion rotate = CalculateValue(rootNodeAnimation.rotate.keyframes, animationTime_);
      Vector3 scale = CalculateValue(rootNodeAnimation.scale.keyframes, animationTime_);
      localMatrix = MakeAffineMatrix(scale, rotate, translate);
  } else if (model_) {
      localMatrix = model_->GetRootLocalMatrix();
  }

  // スキニング描画の場合、パレット(skeletonSpaceMatrix)の計算ですでにRootNodeの変換が含まれているため、
  // ここで重ねてlocalMatrixを掛けると二重に変換がかかってしまう(極端に縮小される等)。
  // したがって、SkinClusterがある場合はlocalMatrixを単位行列にする。
  if (skinCluster_) {
      localMatrix = MakeIdentity4x4();
  }

  Matrix4x4 worldMatrix = MakeAffineMatrix(transform_.scale, transform_.rotate,
                                           transform_.translate);

  if (parent_) {
      worldMatrix = Multiply(worldMatrix, parent_->GetWorldMatrix());
  }

  Matrix4x4 finalWorld = Multiply(localMatrix, worldMatrix);
  worldMatrix_ = finalWorld;

  Matrix4x4 worldViewProjectionMatrix;

  if (activeCamera) {
    const Matrix4x4 &vp = activeCamera->GetViewProjectionMatrix();
    worldViewProjectionMatrix = Multiply(finalWorld, vp);
    cameraForGPUData->worldPosition = activeCamera->GetTranslate();
  } else {
    worldViewProjectionMatrix = finalWorld;
  }

  transformationMatrixData->WVP = worldViewProjectionMatrix;
  transformationMatrixData->World = finalWorld;
  transformationMatrixData->WorldInverseTranspose =
      Transpose(Inverse(finalWorld));
}

void Object3d::Draw() {

  assert(dx12Core_ && "Object3d::Draw: not initialized");
  assert(object3dRenderer_ && "Object3d::Draw: not initialized");
  assert(materialResource && "Object3d::Draw: materialResource is null");
  assert(transformationMatrixResource &&
         "Object3d::Draw: transformationMatrixResource is null");
  assert(cameraForGPUResource &&
         "Object3d::Draw: cameraForGPUResource is null");

  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList =
      dx12Core_->GetCommandList();
  assert(commandList && "Object3d::Draw: commandList is null");

  // マテリアルCBufferの場所を設定
  commandList->SetGraphicsRootConstantBufferView(
      0, materialResource->GetGPUVirtualAddress());

  // wvp用のCBufferの場所を設定
  commandList->SetGraphicsRootConstantBufferView(
      1, transformationMatrixResource->GetGPUVirtualAddress());

  // Lighting
  commandList->SetGraphicsRootConstantBufferView(
      3,
      object3dRenderer_->GetDirectionalLightResource()->GetGPUVirtualAddress());

  // Camera
  commandList->SetGraphicsRootConstantBufferView(
      4, cameraForGPUResource->GetGPUVirtualAddress());

  // PointLight
  commandList->SetGraphicsRootConstantBufferView(
      5, object3dRenderer_->GetPointLightResource()->GetGPUVirtualAddress());

  // SpotLight
  commandList->SetGraphicsRootConstantBufferView(
      6, object3dRenderer_->GetSpotLightResource()->GetGPUVirtualAddress());

  // Environment Map (Skybox)
  commandList->SetGraphicsRootDescriptorTable(
      7, TextureManager::GetInstance()->GetSrvHandleGPU("resources/Skybox/Skybox.dds"));

  // Mask Texture for Dissolve
  commandList->SetGraphicsRootDescriptorTable(
      8, TextureManager::GetInstance()->GetSrvHandleGPU(maskTexturePath_));

  // Fog
  commandList->SetGraphicsRootConstantBufferView(
      9, object3dRenderer_->GetFogResource()->GetGPUVirtualAddress());

  // 3Dモデルが割り当てられていれば描画する
  if (model_) {
    model_->Draw(skinCluster_);
  }
}

void Object3d::SetModel(const std::string &filePath) {
  modelFilePath_ = filePath;
  
  // 自動で読み込む
  ModelManager::GetInstance()->LoadModel(filePath);
  
  // モデルを検索し、セット
  model_ = ModelManager::GetInstance()->FindModel(filePath);

  // LoadModel と SetModel の引数不一致（スペルミス等）を即座に検知する
  if (!model_) {
    Logger::Log(std::string("[Object3d] SetModel failed. Model not found: ") +
                filePath);
    Logger::Log("[Object3d] Falling back to ERROR model (magenta cube).");

    // リリースビルドでのクラッシュを防ぐため、目立つエラー用モデルをセットする
    std::string fallbackPath = "error.obj";
    ModelManager::GetInstance()->LoadModel(fallbackPath);
    model_ = ModelManager::GetInstance()->FindModel(fallbackPath);

    // 開発中はエラーに気づけるようアサートを残す（リリースビルドでは無視され進行する）
    assert(false && "Object3d::SetModel: model not loaded / not found (Fallback applied)");
  }

  // モデルのデフォルトマテリアルをコピー
  if (materialData && model_) {
    *materialData = model_->GetDefaultMaterial();
  }
}

void Object3d::SetMaskTexturePath(const std::string& path) {
  maskTexturePath_ = path;
  TextureManager::GetInstance()->LoadTexture(maskTexturePath_);
}

void Object3d::CreateTransformationMatrixData() {

  // cbvのsizeは256バイト単位で確保する
  UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

  assert(transformationMatrixSize % 256 == 0);

  // データを書き込む
  transformationMatrixResource =
      dx12Core_->CreateBufferResource(transformationMatrixSize);

  // 書き込むためのアドレスを取得
  HRESULT hr = transformationMatrixResource->Map(
      0, nullptr, reinterpret_cast<void **>(&transformationMatrixData));
  assert(SUCCEEDED(hr));
  assert(transformationMatrixData);

  // 単位行列を書き込んでおく
  transformationMatrixData->World = MakeIdentity4x4();
  transformationMatrixData->WVP = MakeIdentity4x4();
  transformationMatrixData->WorldInverseTranspose = MakeIdentity4x4();
}

void Object3d::CreateCameraForGPUData() {

  UINT cameraSize = (sizeof(CameraForGPU) + 255) & ~255;
  cameraForGPUResource = dx12Core_->CreateBufferResource(cameraSize);

  // データを書き込む
  cameraForGPUData = nullptr;

  // 書き込むためのアドレスを取得
  HRESULT hr = cameraForGPUResource->Map(
      0, nullptr, reinterpret_cast<void **>(&cameraForGPUData));
  assert(SUCCEEDED(hr));
  assert(cameraForGPUData);

  cameraForGPUData->worldPosition = {0.0f, 0.0f, 0.0f};
}

void Object3d::CreateMaterialData() {

  UINT materialSize = (sizeof(Model::Material) + 255) & ~255;
  assert(materialSize % 256 == 0);

  materialResource = dx12Core_->CreateBufferResource(materialSize);

  // マテリアルにデータを書き込む
  materialData = nullptr;

  // 書き込むためのアドレスを取得
  HRESULT hr = materialResource->Map(0, nullptr,
                                     reinterpret_cast<void **>(&materialData));
  assert(SUCCEEDED(hr));
  assert(materialData);

  // 色の指定
  materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
  // Lightingさせるか
  materialData->enableLighting = true;
  // UVTransform 単位行列を入れておく
  materialData->uvTransform = MakeIdentity4x4();
  materialData->shininess = 30.0f;
  materialData->environmentCoefficient = 0.0f;
  materialData->rimColor = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
  materialData->rimPower = 3.0f;

  materialData->enableDissolve = 0;
  materialData->dissolveThreshold = 0.0f;
  materialData->dissolveEdgeRange = 0.05f;
  materialData->maskTransform = {0.0f, 0.0f};
  materialData->dissolveEdgeColor = Vector4(1.0f, 0.4f, 0.3f, 1.0f);
}
