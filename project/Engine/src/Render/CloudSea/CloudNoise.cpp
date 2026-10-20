#include "Render/CloudSea/CloudNoise.h"
#include "Core/SrvManager.h"
#include "Debug/Logger.h"
#include <cassert>

namespace {
// 作るノイズの種類（シェーダーの kind と同じ）
constexpr uint32_t kKindBase = 0;
constexpr uint32_t kKindDetail = 1;
} // namespace

void CloudNoise::Initialize(Dx12Core *dx12Core, SrvManager *srvManager) {
  dx12Core_ = dx12Core;
  srvManager_ = srvManager;

  CreateTexture(base_, kBaseSize_, true, DXGI_FORMAT_R8G8B8A8_UNORM);
  CreateTexture(detail_, kDetailSize_, true, DXGI_FORMAT_R8G8B8A8_UNORM);
  CreateTexture(top_, kTopSize_, false, DXGI_FORMAT_R32_FLOAT);
  CreatePSO();
}

void CloudNoise::CreateTexture(NoiseTexture &texture, uint32_t size,
                               bool isVolume, DXGI_FORMAT format) {
  ID3D12Device *device = dx12Core_->GetDevice();
  texture.size = size;

  D3D12_RESOURCE_DESC resourceDesc{};
  resourceDesc.Dimension = isVolume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                    : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Width = size;
  resourceDesc.Height = size;
  resourceDesc.DepthOrArraySize = isVolume ? UINT16(size) : 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.Format = format;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES heapProperties{};
  heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

  HRESULT hr = device->CreateCommittedResource(
      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&texture.resource));
  assert(SUCCEEDED(hr));
  dx12Core_->RegisterResourceState(texture.resource.Get(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // 読む用（ピクセルシェーダー）
  texture.srvIndex = srvManager_->Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = resourceDesc.Format;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  if (isVolume) {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srvDesc.Texture3D.MipLevels = 1;
  } else {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
  }
  device->CreateShaderResourceView(
      texture.resource.Get(), &srvDesc,
      srvManager_->GetCPUDescriptorHandle(texture.srvIndex));

  // 書く用（計算シェーダー）
  texture.uavIndex = srvManager_->Allocate();
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
  uavDesc.Format = resourceDesc.Format;
  if (isVolume) {
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    uavDesc.Texture3D.WSize = size;
  } else {
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  }
  device->CreateUnorderedAccessView(
      texture.resource.Get(), nullptr, &uavDesc,
      srvManager_->GetCPUDescriptorHandle(texture.uavIndex));
}

void CloudNoise::CreateRootSignature() {
  ID3D12Device *device = dx12Core_->GetDevice();

  //========================
  // RootParameter
  // 0 : 作り方の指示（b0、32 ビットの値 2 つ）
  // 1 : 書き込むテクスチャ（u0）
  //========================
  D3D12_ROOT_PARAMETER rootParameters[2] = {};

  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].Constants.ShaderRegister = 0;
  rootParameters[0].Constants.Num32BitValues =
      sizeof(ParamsForGPU) / sizeof(uint32_t);

  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
  descriptorRange[0].BaseShaderRegister = 0;
  descriptorRange[0].NumDescriptors = 1;
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRange;
  rootParameters[1].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange);

  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);

  Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
  HRESULT hr = D3D12SerializeRootSignature(
      &descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1,
      signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
  if (FAILED(hr)) {
    if (errorBlob) {
      Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    }
    assert(false);
  }

  hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                   signatureBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&rootSignature_));
  assert(SUCCEEDED(hr));
}

void CloudNoise::CreatePSO() {
  CreateRootSignature();

  IDxcBlob *computeShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudNoise.CS.hlsl", L"cs_6_0");
  assert(computeShaderBlob != nullptr);

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.CS = {computeShaderBlob->GetBufferPointer(),
                computeShaderBlob->GetBufferSize()};
  psoDesc.pRootSignature = rootSignature_.Get();

  HRESULT hr = dx12Core_->GetDevice()->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&pipelineState_));
  assert(SUCCEEDED(hr));

  IDxcBlob *topShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudTopNoise.CS.hlsl", L"cs_6_0");
  assert(topShaderBlob != nullptr);
  psoDesc.CS = {topShaderBlob->GetBufferPointer(),
                topShaderBlob->GetBufferSize()};
  hr = dx12Core_->GetDevice()->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&topPipelineState_));
  assert(SUCCEEDED(hr));
}

void CloudNoise::Dispatch(const NoiseTexture &texture, uint32_t kind,
                          uint32_t groupSize, bool isVolume) {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  const ParamsForGPU params = {texture.size, kind};
  commandList->SetComputeRoot32BitConstants(
      0, sizeof(ParamsForGPU) / sizeof(uint32_t), &params, 0);
  commandList->SetComputeRootDescriptorTable(
      1, srvManager_->GetGPUDescriptorHandle(texture.uavIndex));

  const UINT groups = (texture.size + groupSize - 1) / groupSize;
  commandList->Dispatch(groups, groups, isVolume ? groups : 1);
}

void CloudNoise::GenerateIfNeeded() {
  if (generated_) {
    return;
  }
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  commandList->SetComputeRootSignature(rootSignature_.Get());
  commandList->SetPipelineState(pipelineState_.Get());
  Dispatch(base_, kKindBase, kVolumeThreadGroupSize_, true);
  Dispatch(detail_, kKindDetail, kVolumeThreadGroupSize_, true);
  commandList->SetPipelineState(topPipelineState_.Get());
  Dispatch(top_, 0, kTopThreadGroupSize_, false);

  // 書き終わったらピクセルシェーダーで読む状態にする
  dx12Core_->TransitionResource(base_.resource.Get(),
                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  dx12Core_->TransitionResource(detail_.resource.Get(),
                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  dx12Core_->TransitionResource(top_.resource.Get(),
                                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  generated_ = true;
}

D3D12_GPU_DESCRIPTOR_HANDLE CloudNoise::GetBaseSrvHandle() const {
  return srvManager_->GetGPUDescriptorHandle(base_.srvIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE CloudNoise::GetDetailSrvHandle() const {
  return srvManager_->GetGPUDescriptorHandle(detail_.srvIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE CloudNoise::GetTopSrvHandle() const {
  return srvManager_->GetGPUDescriptorHandle(top_.srvIndex);
}
