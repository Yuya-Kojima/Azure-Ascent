#include "Core/SrvManager.h"
#include "Debug/Logger.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <format>

const uint32_t SrvManager::kMaxSRVCount = 2048;

SrvManager::~SrvManager() {
  // まだ返されていない番号があれば、その数をログに出す
  if (usedCount_ != 0) {
    Logger::Log(std::format(
        "SrvManager destroyed with {} SRV descriptors still allocated\n",
        usedCount_));
  }
}

void SrvManager::Initialize(Dx12Core *dx12Core) {
  dx12Core_ = dx12Core;

  isAllocated_.assign(kMaxSRVCount, false);
  freeIndices_.reserve(kMaxSRVCount);

  // ディスクリプタヒープの生成
  descriptorHeap = dx12Core_->CreateDescriptorHeap(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxSRVCount, true);

  // ディスクリプタ一個分のサイズを取得して記録
  descriptorSize = dx12Core_->GetDevice()->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

SrvHandle SrvManager::Allocate() {

  // 空きも新しい番号もなければ、使用数と上限をログとメッセージの窓に出して止める
  if (freeIndices_.empty() && useIndex_ >= kMaxSRVCount) {
    const std::string message =
        std::format("Out of SRV descriptors (used {} / capacity {})\n",
                    usedCount_, kMaxSRVCount);
    Logger::Log(message);
    MessageBoxA(nullptr, message.c_str(), "SRV Error", MB_OK | MB_ICONERROR);
    std::abort();
  }

  uint32_t index = 0;
  if (!freeIndices_.empty()) {
    // 返された番号を使い回す
    index = freeIndices_.back();
    freeIndices_.pop_back();
  } else {
    // まだ使っていない番号を進める
    index = useIndex_;
    useIndex_++;
  }

  isAllocated_[index] = true;
  usedCount_++;
  peakCount_ = (std::max)(peakCount_, usedCount_);

  return SrvHandle(this, index);
}

void SrvManager::Free(uint32_t index) {
  // 範囲外と二重返却を検出する
  assert(index < useIndex_);
  assert(isAllocated_[index]);

  isAllocated_[index] = false;
  freeIndices_.push_back(index);
  usedCount_--;
}

D3D12_CPU_DESCRIPTOR_HANDLE SrvManager::GetCPUDescriptorHandle(uint32_t index) {
  D3D12_CPU_DESCRIPTOR_HANDLE handleCPU =
      descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  handleCPU.ptr += (descriptorSize * index);
  return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE SrvManager::GetGPUDescriptorHandle(uint32_t index) {
  D3D12_GPU_DESCRIPTOR_HANDLE handleGPU =
      descriptorHeap->GetGPUDescriptorHandleForHeapStart();
  handleGPU.ptr += (descriptorSize * index);
  return handleGPU;
}

void SrvManager::CreateSRVforTexture2D(uint32_t srvIndex,
                                       ID3D12Resource *pResource,
                                       const DirectX::TexMetadata &metadata) {

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = metadata.format;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  if (metadata.IsCubemap()) {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = UINT_MAX;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
  } else {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
  }

  dx12Core_->GetDevice()->CreateShaderResourceView(
      pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforDepth(uint32_t srvIndex, ID3D12Resource *pResource) {
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  dx12Core_->GetDevice()->CreateShaderResourceView(
      pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateSRVforStructuredBuffer(uint32_t srvIndex,
                                              ID3D12Resource *pResource,
                                              UINT numElements,
                                              UINT structureByteStride) {

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
  srvDesc.Buffer.FirstElement = 0;
  srvDesc.Buffer.NumElements = numElements;
  srvDesc.Buffer.StructureByteStride = structureByteStride;
  srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

  dx12Core_->GetDevice()->CreateShaderResourceView(
      pResource, &srvDesc, GetCPUDescriptorHandle(srvIndex));
}

void SrvManager::CreateUAVforStructuredBuffer(uint32_t srvIndex,
                                              ID3D12Resource *pResource,
                                              UINT numElements,
                                              UINT structureByteStride) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
  uavDesc.Format = DXGI_FORMAT_UNKNOWN;
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
  uavDesc.Buffer.FirstElement = 0;
  uavDesc.Buffer.NumElements = numElements;
  uavDesc.Buffer.StructureByteStride = structureByteStride;
  uavDesc.Buffer.CounterOffsetInBytes = 0;
  uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

  dx12Core_->GetDevice()->CreateUnorderedAccessView(
      pResource, nullptr, &uavDesc, GetCPUDescriptorHandle(srvIndex));
}


void SrvManager::PreDraw() {

  // 描画用のDescriptorHeapの設定
  ID3D12DescriptorHeap *descriptorHeaps[] = {descriptorHeap.Get()};
  dx12Core_->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);
}

void SrvManager::SetGraphicsRootDescriptorTable(UINT rootParameterIndex,
                                                uint32_t srvIndex) {
  dx12Core_->GetCommandList()->SetGraphicsRootDescriptorTable(
      rootParameterIndex, GetGPUDescriptorHandle(srvIndex));
}
