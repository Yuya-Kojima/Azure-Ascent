#include "Core/ConstantBufferHandle.h"
#include "Core/ConstantBufferManager.h"
#include <cassert>

ConstantBufferHandle::ConstantBufferHandle(ConstantBufferManager *manager,
                                           uint32_t firstBlock,
                                           uint32_t blockCount,
                                           void *cpuAddress,
                                           D3D12_GPU_VIRTUAL_ADDRESS gpuAddress)
    : manager_(manager), firstBlock_(firstBlock), blockCount_(blockCount),
      cpuAddress_(cpuAddress), gpuAddress_(gpuAddress) {}

ConstantBufferHandle::~ConstantBufferHandle() { Reset(); }

ConstantBufferHandle::ConstantBufferHandle(ConstantBufferHandle &&other) noexcept
    : manager_(other.manager_), firstBlock_(other.firstBlock_),
      blockCount_(other.blockCount_), cpuAddress_(other.cpuAddress_),
      gpuAddress_(other.gpuAddress_) {
  other.Clear();
}

ConstantBufferHandle &
ConstantBufferHandle::operator=(ConstantBufferHandle &&other) noexcept {
  if (this != &other) {
    // いま持っている枠を返してから、other の枠を受け取る
    Reset();
    manager_ = other.manager_;
    firstBlock_ = other.firstBlock_;
    blockCount_ = other.blockCount_;
    cpuAddress_ = other.cpuAddress_;
    gpuAddress_ = other.gpuAddress_;
    other.Clear();
  }
  return *this;
}

void *ConstantBufferHandle::GetCpuAddress() const {
  assert(IsValid());
  return cpuAddress_;
}

D3D12_GPU_VIRTUAL_ADDRESS ConstantBufferHandle::GetGpuAddress() const {
  assert(IsValid());
  return gpuAddress_;
}

void ConstantBufferHandle::Reset() {
  if (manager_) {
    manager_->Free(firstBlock_, blockCount_);
    Clear();
  }
}

void ConstantBufferHandle::Clear() {
  manager_ = nullptr;
  firstBlock_ = 0;
  blockCount_ = 0;
  cpuAddress_ = nullptr;
  gpuAddress_ = 0;
}
