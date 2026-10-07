#include "Render/SkyBox/SkyBox.h"
#include "Camera/ICamera.h"
#include "Render/Renderer/SkyBoxRenderer.h"
#include "Texture/TextureManager.h"
#include <cassert>
#include <cstring>

void Skybox::Initialize(SkyboxRenderer *skyboxRenderer) {
  skyboxRenderer_ = skyboxRenderer;
  dx12Core_ = skyboxRenderer_->GetDx12Core();

  CreateVertices();
  CreateVertexResource();
  CreateTransformationMatrixResource();
  CreateMaterialResource();
}

void Skybox::Update() {

  assert(transformationMatrixData_);
  assert(materialData_);

  Vector3 translation = transform_.translate;

  if (camera_) {
    translation = camera_->GetTranslate();
  }

  Matrix4x4 worldMatrix =
      MakeAffineMatrix(transform_.scale, transform_.rotate, translation);

  Matrix4x4 worldViewProjectionMatrix = worldMatrix;

  if (camera_) {
    worldViewProjectionMatrix =
        Multiply(worldMatrix, camera_->GetViewProjectionMatrix());
  }

  transformationMatrixData_->World = worldMatrix;
  transformationMatrixData_->WVP = worldViewProjectionMatrix;
}

void Skybox::Draw() {

  assert(dx12Core_);
  assert(materialResource_);
  assert(transformationMatrixResource_);
  assert(!textureFilePath_.empty());

  auto commandList = dx12Core_->GetCommandList();
  assert(commandList);

  // Material : root parameter 0
  commandList->SetGraphicsRootConstantBufferView(
      0, materialResource_->GetGPUVirtualAddress());

  // Transformation : root parameter 1
  commandList->SetGraphicsRootConstantBufferView(
      1, transformationMatrixResource_->GetGPUVirtualAddress());

  // TextureCube SRV : root parameter 2
  commandList->SetGraphicsRootDescriptorTable(
      2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));

  // VertexBuffer
  commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

  // Draw
  commandList->DrawInstanced(UINT(vertices_.size()), 1, 0, 0);
}

void Skybox::CreateVertices() {
  vertices_.clear();
  vertices_.reserve(36);

  const Vector2 uv = {0.0f, 0.0f};
  const Vector3 normal = {0.0f, 0.0f, 0.0f};

  // 右
  vertices_.push_back({{1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});

  // 左
  vertices_.push_back({{-1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});

  // 前
  vertices_.push_back({{-1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});

  // 後
  vertices_.push_back({{1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});

  // 上
  vertices_.push_back({{-1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, 1.0f, 1.0f, 1.0f}, uv, normal});

  // 下
  vertices_.push_back({{-1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{-1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, 1.0f, 1.0f}, uv, normal});
  vertices_.push_back({{1.0f, -1.0f, -1.0f, 1.0f}, uv, normal});
}

void Skybox::CreateVertexResource() {
  vertexResource_ =
      dx12Core_->CreateBufferResource(sizeof(VertexData) * vertices_.size());

  vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
  vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices_.size());
  vertexBufferView_.StrideInBytes = sizeof(VertexData);

  vertexResource_->Map(0, nullptr, reinterpret_cast<void **>(&vertexData_));
  std::memcpy(vertexData_, vertices_.data(),
              sizeof(VertexData) * vertices_.size());
}

void Skybox::CreateTransformationMatrixResource() {

  UINT size = (sizeof(TransformationMatrix) + 255) & ~255;
  transformationMatrixResource_ = dx12Core_->CreateBufferResource(size);

  HRESULT hr = transformationMatrixResource_->Map(
      0, nullptr, reinterpret_cast<void **>(&transformationMatrixData_));
  assert(SUCCEEDED(hr));
  assert(transformationMatrixData_);

  transformationMatrixData_->WVP = MakeIdentity4x4();
  transformationMatrixData_->World = MakeIdentity4x4();
}

void Skybox::CreateMaterialResource() {

  UINT size = (sizeof(Material) + 255) & ~255;
  materialResource_ = dx12Core_->CreateBufferResource(size);

  HRESULT hr = materialResource_->Map(
      0, nullptr, reinterpret_cast<void **>(&materialData_));
  assert(SUCCEEDED(hr));
  assert(materialData_);

  materialData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
  materialData_->skyParams = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
  materialData_->zenithColor = Vector4(0.12f, 0.38f, 0.85f, 1.0f);
  materialData_->horizonColor = Vector4(0.72f, 0.86f, 1.0f, 1.0f);
  materialData_->sunDir = Vector4(0.0f, 0.5f, 0.85f, 0.5f);
  materialData_->sunColor = Vector4(1.0f, 0.95f, 0.85f, 1.0f);
}
