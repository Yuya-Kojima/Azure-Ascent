#include "Debug/GpuTimer.h"
#include <cassert>

void GpuTimer::Initialize(Dx12Core *dx12Core) {
  dx12Core_ = dx12Core;
  ID3D12Device *device = dx12Core_->GetDevice();

  D3D12_QUERY_HEAP_DESC queryHeapDesc{};
  queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  queryHeapDesc.Count = kQueryCount_;
  HRESULT hr = device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&queryHeap_));
  assert(SUCCEEDED(hr));

  // CPU から読める置き場（読み出し用のヒープ）
  D3D12_HEAP_PROPERTIES heapProperties{};
  heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC resourceDesc{};
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  resourceDesc.Width = sizeof(uint64_t) * kQueryCount_;
  resourceDesc.Height = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.MipLevels = 1;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  hr = device->CreateCommittedResource(
      &heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&readbackResource_));
  assert(SUCCEEDED(hr));

  hr = dx12Core_->GetCommandQueue()->GetTimestampFrequency(&frequency_);
  assert(SUCCEEDED(hr));
}

void GpuTimer::Begin() {
  dx12Core_->GetCommandList()->EndQuery(queryHeap_.Get(),
                                        D3D12_QUERY_TYPE_TIMESTAMP, 0);
}

void GpuTimer::End() {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();
  commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
  commandList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                0, kQueryCount_, readbackResource_.Get(), 0);
  hasPendingResult_ = true;
}

float GpuTimer::GetMilliseconds() {
  // フレームの終わりに GPU を待っているので、前のフレームで積んだ結果は読める
  if (hasPendingResult_) {
    const D3D12_RANGE readRange = {0, sizeof(uint64_t) * kQueryCount_};
    uint64_t *timestamps = nullptr;
    HRESULT hr = readbackResource_->Map(
        0, &readRange, reinterpret_cast<void **>(&timestamps));
    if (SUCCEEDED(hr) && timestamps) {
      const float latest = float(double(timestamps[1] - timestamps[0]) /
                                 double(frequency_) * 1000.0);
      milliseconds_ += (latest - milliseconds_) * kSmoothing_;
      const D3D12_RANGE writtenRange = {0, 0};
      readbackResource_->Unmap(0, &writtenRange);
    }
    hasPendingResult_ = false;
  }
  return milliseconds_;
}
