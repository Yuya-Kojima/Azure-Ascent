#include "Render/CloudSea/CloudSea.h"
#include "Camera/ICamera.h"
#include "Renderer/CloudSeaRenderer.h"
#include "Texture/TextureManager.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>

void CloudSea::Initialize(CloudSeaRenderer *renderer,
                          const std::string &texturePath) {
  renderer_ = renderer;
  dx12Core_ = renderer_->GetDx12Core();
  texturePath_ = texturePath;
  TextureManager::GetInstance()->LoadTexture(texturePath_);

  CreateGrid();
  CreateParamsResource();
}

void CloudSea::CreateGrid() {
  // 段 l の頂点 (i, j) は網の中心から (i, j) × 目の幅 の位置（i, j は -kHalfCells_ 〜 kHalfCells_）
  const int side = kHalfCells_ * 2 + 1;
  std::vector<Vector2> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(size_t(side) * side * kLevelCount_);

  float spacing = kFinestSpacing_;
  for (int level = 0; level < kLevelCount_; ++level) {
    const uint32_t base = uint32_t(vertices.size());
    const bool hasOuterLevel = level + 1 < kLevelCount_;

    for (int j = -kHalfCells_; j <= kHalfCells_; ++j) {
      for (int i = -kHalfCells_; i <= kHalfCells_; ++i) {
        // 外の縁に近いほど、奇数番目の頂点を一つ手前の偶数番目へ寄せる（縁で外の段の目と一致する）
        float morph = 0.0f;
        if (hasOuterLevel) {
          const int ring = (std::max)(std::abs(i), std::abs(j));
          morph = std::clamp(
              float(ring - (kHalfCells_ - kMorphCells_)) / float(kMorphCells_),
              0.0f, 1.0f);
        }
        const float x = (float(i) - (i & 1 ? morph : 0.0f)) * spacing;
        const float z = (float(j) - (j & 1 ? morph : 0.0f)) * spacing;
        vertices.push_back({x, z});
      }
    }

    // 2 段目からは、内側の段が覆う真ん中（±kHalfCells_ / 2）を空けた輪にする
    const int hole = kHalfCells_ / 2;
    for (int j = -kHalfCells_; j < kHalfCells_; ++j) {
      for (int i = -kHalfCells_; i < kHalfCells_; ++i) {
        if (level > 0 && i >= -hole && i + 1 <= hole && j >= -hole &&
            j + 1 <= hole) {
          continue;
        }
        const uint32_t v00 =
            base + uint32_t((j + kHalfCells_) * side + (i + kHalfCells_));
        const uint32_t v10 = v00 + 1;
        const uint32_t v01 = v00 + uint32_t(side);
        const uint32_t v11 = v01 + 1;
        indices.insert(indices.end(), {v00, v01, v10, v10, v01, v11});
      }
    }
    spacing *= 2.0f;
  }
  indexCount_ = UINT(indices.size());

  const size_t vertexBytes = sizeof(Vector2) * vertices.size();
  vertexResource_ = dx12Core_->CreateBufferResource(vertexBytes);
  vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
  vertexBufferView_.SizeInBytes = UINT(vertexBytes);
  vertexBufferView_.StrideInBytes = sizeof(Vector2);
  void *mapped = nullptr;
  vertexResource_->Map(0, nullptr, &mapped);
  std::memcpy(mapped, vertices.data(), vertexBytes);
  vertexResource_->Unmap(0, nullptr);

  const size_t indexBytes = sizeof(uint32_t) * indices.size();
  indexResource_ = dx12Core_->CreateBufferResource(indexBytes);
  indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
  indexBufferView_.SizeInBytes = UINT(indexBytes);
  indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
  indexResource_->Map(0, nullptr, &mapped);
  std::memcpy(mapped, indices.data(), indexBytes);
  indexResource_->Unmap(0, nullptr);
}

void CloudSea::CreateParamsResource() {
  UINT size = (sizeof(ParamsForGPU) + 255) & ~255;
  paramsResource_ = dx12Core_->CreateBufferResource(size);

  HRESULT hr = paramsResource_->Map(0, nullptr,
                                    reinterpret_cast<void **>(&paramsData_));
  assert(SUCCEEDED(hr));
  assert(paramsData_);
  std::memset(paramsData_, 0, sizeof(ParamsForGPU));
}

void CloudSea::ClearShapes() { shapeCount_ = 0; }

void CloudSea::SetShape(int index, const CloudSeaShape &shape) {
  if (index < 0 || index >= kMaxShapes) {
    return;
  }
  shapes_[index] = shape;
  shapeCount_ = (std::max)(shapeCount_, index + 1);
}

void CloudSea::Update() {
  assert(paramsData_);

  // 網の中心をカメラの真下に置く。一番粗い目の幅で丸めて、どの段の頂点もワールドの決まった位置に乗せる
  const float coarsestSpacing =
      kFinestSpacing_ * float(1 << (kLevelCount_ - 1));
  Vector3 cameraPosition = {0.0f, 0.0f, 0.0f};
  if (camera_) {
    cameraPosition = camera_->GetTranslate();
    paramsData_->viewProjection = camera_->GetViewProjectionMatrix();
  } else {
    paramsData_->viewProjection = MakeIdentity4x4();
  }
  paramsData_->gridOrigin = {
      std::round(cameraPosition.x / coarsestSpacing) * coarsestSpacing,
      std::round(cameraPosition.z / coarsestSpacing) * coarsestSpacing};

  paramsData_->cameraPosition = cameraPosition;
  paramsData_->time = time_;
  paramsData_->height = height_;
  paramsData_->amplitude = amplitude_;
  paramsData_->wind = wind_;
  paramsData_->blobSize = (std::max)(blobSize_, 0.01f);
  paramsData_->valleyDarkness = valleyDarkness_;
  paramsData_->color = {color_.x, color_.y, color_.z, 1.0f};
  paramsData_->rimStrength = rimStrength_;
  paramsData_->rimPower = rimPower_;
  paramsData_->textureScale = textureScale_;
  paramsData_->shapeCount = shapeCount_;
  for (int i = 0; i < shapeCount_; ++i) {
    const CloudSeaShape &s = shapes_[i];
    paramsData_->shapes[i] = {s.center, s.radius,        s.width,
                              s.amount, s.crack, int32_t(s.kind), 0.0f};
  }
}

void CloudSea::Draw() {
  assert(dx12Core_);
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();
  assert(commandList);

  renderer_->Begin();

  // 雲海の値と形 : root parameter 0
  commandList->SetGraphicsRootConstantBufferView(
      0, paramsResource_->GetGPUVirtualAddress());
  // 模様のテクスチャ : root parameter 3
  commandList->SetGraphicsRootDescriptorTable(
      3, TextureManager::GetInstance()->GetSrvHandleGPU(texturePath_));

  commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
  commandList->IASetIndexBuffer(&indexBufferView_);
  commandList->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}
