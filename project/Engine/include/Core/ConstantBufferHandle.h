#pragma once
#include <cstdint>
#include <d3d12.h>

class ConstantBufferManager;

/// <summary>
/// 定数バッファの置き場から借りた枠 1 つの持ち主。壊れると枠を ConstantBufferManager に返す。
/// コピーはできず、ムーブすると持ち主が移る（移した側は空になる）。
/// 枠を返す先の ConstantBufferManager は、すべてのハンドルより後に壊す
/// </summary>
class ConstantBufferHandle {
public:
  ConstantBufferHandle() = default;
  ConstantBufferHandle(ConstantBufferManager *manager, uint32_t firstBlock,
                       uint32_t blockCount, void *cpuAddress,
                       D3D12_GPU_VIRTUAL_ADDRESS gpuAddress);
  ~ConstantBufferHandle();

  ConstantBufferHandle(const ConstantBufferHandle &) = delete;
  ConstantBufferHandle &operator=(const ConstantBufferHandle &) = delete;

  ConstantBufferHandle(ConstantBufferHandle &&other) noexcept;
  ConstantBufferHandle &operator=(ConstantBufferHandle &&other) noexcept;

  /// <summary>
  /// CPU から書き込む先（空のハンドルでは呼ばない）
  /// </summary>
  void *GetCpuAddress() const;

  /// <summary>
  /// シェーダーに渡す GPU のアドレス（空のハンドルでは呼ばない）
  /// </summary>
  D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress() const;

  bool IsValid() const { return manager_ != nullptr; }

  /// <summary>
  /// 枠を ConstantBufferManager に返して空になる
  /// </summary>
  void Reset();

private:
  // 中身を空にする（返しはしない）
  void Clear();

  ConstantBufferManager *manager_ = nullptr;
  // 枠の先頭の番号（256 バイトを 1 つと数える）
  uint32_t firstBlock_ = 0;
  // 枠の大きさ（256 バイトがいくつ分か）
  uint32_t blockCount_ = 0;
  void *cpuAddress_ = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS gpuAddress_ = 0;
};
