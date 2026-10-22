#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvHandle.h"
#include <cstdint>
#include <wrl.h>

class SrvManager;

/// <summary>
/// 雲の形に使うノイズのテクスチャ（3 次元の大きなノイズ・細かいノイズと、上の面の起伏の 2 次元ノイズの 3 枚）。
/// 最初に使うときに計算シェーダーで一度だけ作る
/// </summary>
class CloudNoise {
public:
  void Initialize(Dx12Core *dx12Core, SrvManager *srvManager);

  /// <summary>
  /// まだ作っていなければ、ノイズを作る命令をコマンドリストに積む
  /// </summary>
  void GenerateIfNeeded();

  D3D12_GPU_DESCRIPTOR_HANDLE GetBaseSrvHandle() const;
  D3D12_GPU_DESCRIPTOR_HANDLE GetDetailSrvHandle() const;
  D3D12_GPU_DESCRIPTOR_HANDLE GetTopSrvHandle() const;

private:
  // 1 枚分のテクスチャと、読む・書く用のディスクリプタの番号
  struct NoiseTexture {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    uint32_t size = 0;
    SrvHandle srv;
    SrvHandle uav;
  };

  // 作り方の指示（シェーダーの NoiseParams と同じ並び）
  struct ParamsForGPU {
    uint32_t size;
    uint32_t kind;
  };

  void CreateTexture(NoiseTexture &texture, uint32_t size, bool isVolume,
                     DXGI_FORMAT format);
  void CreateRootSignature();
  void CreatePSO();
  /// <summary>
  /// texture の全画素を、groupSize 画素四方（3 次元なら立方）ずつのスレッドグループで埋める
  /// </summary>
  void Dispatch(const NoiseTexture &texture, uint32_t kind, uint32_t groupSize,
                bool isVolume);

private:
  // 大きなノイズの一辺の画素数
  static constexpr uint32_t kBaseSize_ = 128;
  // 細かいノイズの一辺の画素数
  static constexpr uint32_t kDetailSize_ = 32;
  // 上の面の起伏のノイズの一辺の画素数
  static constexpr uint32_t kTopSize_ = 1024;
  // 計算シェーダーの 1 グループの一辺のスレッド数（シェーダーの numthreads と同じ）
  static constexpr uint32_t kVolumeThreadGroupSize_ = 4;
  static constexpr uint32_t kTopThreadGroupSize_ = 8;

  Dx12Core *dx12Core_ = nullptr;
  SrvManager *srvManager_ = nullptr;

  NoiseTexture base_;
  NoiseTexture detail_;
  NoiseTexture top_;
  bool generated_ = false;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
  // 3 次元ノイズを作る PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
  // 上の面の起伏の 2 次元ノイズを作る PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> topPipelineState_;
};
