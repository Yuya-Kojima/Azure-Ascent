#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvHandle.h"
#include <vector>

/// <summary>
/// CBV/SRV/UAV ヒープの番号を貸し出す。返された番号は空きリストに入り、次の Allocate で使い回す。
/// 返した番号はすぐ次の持ち主に渡りうるので、返すのは GPU がその番号を使い終えた後に限る。
/// エンジンはフレームの終わり（Dx12Core::PostDraw）で GPU の完了を待つので、前のフレームまでに積んだ命令は終わっている。
/// そのため、今のフレームでまだ命令に使っていない番号（更新中のシーンの切り替えで消える物など）はすぐ返してよい。
/// 複数のフレームを重ねて描く作りに変えるときは、フェンスの値をつけて GPU が通り過ぎてから返す形にする
/// </summary>
class SrvManager {

public:
  ~SrvManager();

  void Initialize(Dx12Core *dx12Core);

  /// <summary>
  /// 番号を 1 つ借りる。空きリストに番号があればそれを使う。
  /// 使い切っていたら、使用数と上限をログに出して止める
  /// </summary>
  SrvHandle Allocate();

  /// <summary>
  /// 番号を空きリストに返す（SrvHandle が壊れるときに呼ぶ）
  /// </summary>
  void Free(uint32_t index);

  // 使用中の番号の数
  uint32_t GetUsedCount() const { return usedCount_; }
  // 起動してからの使用中の数の最大
  uint32_t GetPeakCount() const { return peakCount_; }
  // ヒープの番号の数（上限）
  uint32_t GetCapacity() const { return kMaxSRVCount; }

  D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
  D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

  // SRV生成（テクスチャ用）
  void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource *pResource,
                             const DirectX::TexMetadata &metadata);

  // SRV生成（Depth用）
  void CreateSRVforDepth(uint32_t srvIndex, ID3D12Resource *pResource);

  // SRV生成（Structured Buffer用）
  void CreateSRVforStructuredBuffer(uint32_t srvIndex,
                                    ID3D12Resource *pResource, UINT numElements,
                                    UINT structureByteStride);

  // UAV生成（Structured Buffer用）
  void CreateUAVforStructuredBuffer(uint32_t srvIndex,
                                    ID3D12Resource *pResource, UINT numElements,
                                    UINT structureByteStride);


  void PreDraw();

  void SetGraphicsRootDescriptorTable(UINT rootParameterIndex,
                                      uint32_t srvIndex);

private:
  Dx12Core *dx12Core_ = nullptr;

  // 最大SRV数(最大テクスチャ枚数)
  static const uint32_t kMaxSRVCount;

  // SRV用のディスクリプタサイズ
  uint32_t descriptorSize;

  // SRV用ディスクリプタヒープ
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap;

  // まだ一度も貸していない番号の先頭
  uint32_t useIndex_ = 0;

  // 返されて、次に貸せる番号
  std::vector<uint32_t> freeIndices_;

  // 番号ごとの貸し出し中かどうか（二重返却の検出用）
  std::vector<bool> isAllocated_;

  uint32_t usedCount_ = 0;
  uint32_t peakCount_ = 0;

public:
  ID3D12DescriptorHeap *GetDescriptorHeap() const {
    return descriptorHeap.Get();
  }
};
