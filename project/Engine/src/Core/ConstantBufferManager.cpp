#include "Core/ConstantBufferManager.h"
#include "Core/Dx12Core.h"
#include "Debug/Logger.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <format>

ConstantBufferManager::~ConstantBufferManager() {
  // まだ返されていない枠があれば、その数をログに出す
  if (usedBlockCount_ != 0) {
    Logger::Log(std::format(
        "ConstantBufferManager destroyed with {} blocks still allocated\n",
        usedBlockCount_));
  }
  if (buffer_) {
    buffer_->Unmap(0, nullptr);
  }
}

void ConstantBufferManager::Initialize(Dx12Core *dx12Core) {
  isAllocated_.assign(kCapacityBlockCount_, false);

  // 置き場を 1 つ作り、開いたままにする
  buffer_ = dx12Core->CreateBufferResource(
      static_cast<size_t>(kCapacityBlockCount_) * kBlockSize_);
  HRESULT hr =
      buffer_->Map(0, nullptr, reinterpret_cast<void **>(&cpuBase_));
  assert(SUCCEEDED(hr));
  gpuBase_ = buffer_->GetGPUVirtualAddress();
}

ConstantBufferHandle ConstantBufferManager::Allocate(size_t sizeInBytes) {
  assert(cpuBase_ && "ConstantBufferManager::Initialize が呼ばれていない");

  // 256 バイト単位に切り上げる
  const uint32_t blockCount = static_cast<uint32_t>(
      (std::max)(size_t{1}, (sizeInBytes + kBlockSize_ - 1) / kBlockSize_));

  uint32_t firstBlock = 0;
  std::vector<uint32_t> &freeList = freeBlocks_[blockCount];
  if (!freeList.empty()) {
    // 同じ大きさで返された枠を使い回す
    firstBlock = freeList.back();
    freeList.pop_back();
  } else {
    // 空きも新しい場所もなければ、使用量と上限をログとメッセージの窓に出して止める
    if (nextBlock_ + blockCount > kCapacityBlockCount_) {
      const std::string message = std::format(
          "Out of constant buffer blocks (used {} / capacity {})\n",
          usedBlockCount_, kCapacityBlockCount_);
      Logger::Log(message);
      MessageBoxA(nullptr, message.c_str(), "Constant Buffer Error",
                  MB_OK | MB_ICONERROR);
      std::abort();
    }
    // まだ使っていない場所を進める
    firstBlock = nextBlock_;
    nextBlock_ += blockCount;
  }

  for (uint32_t i = 0; i < blockCount; ++i) {
    isAllocated_[firstBlock + i] = true;
  }
  usedBlockCount_ += blockCount;
  peakBlockCount_ = (std::max)(peakBlockCount_, usedBlockCount_);

  // 前の持ち主の中身が残らないよう 0 で埋める
  const size_t offset = static_cast<size_t>(firstBlock) * kBlockSize_;
  const size_t size = static_cast<size_t>(blockCount) * kBlockSize_;
  std::memset(cpuBase_ + offset, 0, size);

  return ConstantBufferHandle(this, firstBlock, blockCount, cpuBase_ + offset,
                              gpuBase_ + offset);
}

void ConstantBufferManager::Free(uint32_t firstBlock, uint32_t blockCount) {
  // 範囲外と二重返却を検出する
  assert(firstBlock + blockCount <= nextBlock_);
  for (uint32_t i = 0; i < blockCount; ++i) {
    assert(isAllocated_[firstBlock + i]);
    isAllocated_[firstBlock + i] = false;
  }

  freeBlocks_[blockCount].push_back(firstBlock);
  usedBlockCount_ -= blockCount;
}
