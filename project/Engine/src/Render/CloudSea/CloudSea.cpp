#include "Render/CloudSea/CloudSea.h"
#include "Camera/ICamera.h"
#include "Core/WindowSystem.h"
#include "Renderer/CloudSeaRenderer.h"
#include <algorithm>
#include <cassert>
#include <cstring>

void CloudSea::Initialize(CloudSeaRenderer *renderer) {
  renderer_ = renderer;
  dx12Core_ = renderer_->GetDx12Core();

  CreateParamsResource();
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

  if (camera_) {
    paramsData_->inverseViewProjection =
        Inverse(camera_->GetViewProjectionMatrix());
    paramsData_->cameraPosition = camera_->GetTranslate();
    const Matrix4x4 &projection = camera_->GetProjectionMatrix();
    paramsData_->projection22 = projection.m[2][2];
    paramsData_->projection32 = projection.m[3][2];
  } else {
    paramsData_->inverseViewProjection = MakeIdentity4x4();
    paramsData_->cameraPosition = {0.0f, 0.0f, 0.0f};
    paramsData_->projection22 = 1.0f;
    paramsData_->projection32 = 0.0f;
  }
  paramsData_->screenSize = {float(WindowSystem::kClientWidth),
                             float(WindowSystem::kClientHeight)};

  paramsData_->time = time_;
  paramsData_->height = height_;
  paramsData_->amplitude = amplitude_;
  paramsData_->blobSize = (std::max)(blobSize_, 0.01f);
  paramsData_->thickness = (std::max)(look_.thickness, 0.01f);
  paramsData_->wind = wind_;
  paramsData_->edgeSoftness = look_.edgeSoftness;
  paramsData_->density = look_.density;
  paramsData_->color = {look_.color.x, look_.color.y, look_.color.z, 1.0f};
  paramsData_->brightness = look_.brightness;
  paramsData_->powder = look_.powder;
  paramsData_->valleyDarkness = look_.valleyDarkness;
  paramsData_->forwardScatter = look_.forwardScatter;
  paramsData_->rimStrength = look_.rimStrength;
  paramsData_->rimPower = look_.rimPower;
  paramsData_->noiseSize = (std::max)(look_.noiseSize, 0.01f);
  paramsData_->detailSize = (std::max)(look_.detailSize, 0.01f);
  paramsData_->detailStrength = look_.detailStrength;
  paramsData_->jitter = look_.jitter;
  paramsData_->debugView = int32_t(debugView_);
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

  renderer_->Begin(paramsResource_->GetGPUVirtualAddress());

  // 半分の解像度の画面全体を覆う三角形 1 枚で、雲を描く
  commandList->DrawInstanced(3, 1, 0, 0);

  // 元の大きさの画面へ重ねる
  renderer_->Composite();

  renderer_->End();
}
