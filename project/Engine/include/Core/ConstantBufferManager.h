#pragma once
#include "Core/ConstantBufferHandle.h"
#include <cstdint>
#include <d3d12.h>
#include <unordered_map>
#include <vector>
#include <wrl.h>

class Dx12Core;

/// <summary>
/// 定数バッファの置き場。起動時にアップロード用のバッファを 1 つだけ作って開いたままにし、
/// 256 バイト単位の枠に切り分けて貸し出す。返された枠は大きさごとの空きリストに入り、次の Allocate で使い回す。
/// 返した枠はすぐ次の持ち主に渡りうるので、返すのは GPU がその枠を使い終えた後に限る。
/// エンジンはフレームの終わり（Dx12Core::PostDraw）で GPU の完了を待つので、前のフレームまでに積んだ命令は終わっている。
/// 複数のフレームを重ねて描く作りに変えるときは、フェンスの値をつけて GPU が通り過ぎてから返す形にする
/// </summary>
class ConstantBufferManager {

public:
  ~ConstantBufferManager();

  void Initialize(Dx12Core *dx12Core);

  /// <summary>
  /// 枠を 1 つ借りる。中身は 0 で埋めてある。
  /// 使い切っていたら、使用量と上限をログに出して止める
  /// </summary>
  /// <param name="sizeInBytes">使う大きさ（バイト。256 バイト単位に切り上げる）</param>
  ConstantBufferHandle Allocate(size_t sizeInBytes);

  /// <summary>
  /// 枠を空きリストに返す（ConstantBufferHandle が壊れるときに呼ぶ）
  /// </summary>
  void Free(uint32_t firstBlock, uint32_t blockCount);

  // 貸し出し中の 256 バイトの数
  uint32_t GetUsedBlockCount() const { return usedBlockCount_; }
  // 起動してからの貸し出し中の数の最大
  uint32_t GetPeakBlockCount() const { return peakBlockCount_; }
  // 置き場の 256 バイトの数（上限）
  uint32_t GetCapacityBlockCount() const { return kCapacityBlockCount_; }

private:
  // 枠 1 つの最小の大きさ（定数バッファのアドレスは 256 バイト境界にそろえる決まり）
  static constexpr uint32_t kBlockSize_ = 256;
  // 置き場の 256 バイトの数（32768 × 256 バイト = 8MB）
  static constexpr uint32_t kCapacityBlockCount_ = 32768;

  Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
  // 置き場の先頭の CPU のアドレス（開いたまま）
  uint8_t *cpuBase_ = nullptr;
  // 置き場の先頭の GPU のアドレス
  D3D12_GPU_VIRTUAL_ADDRESS gpuBase_ = 0;

  // まだ一度も貸していない 256 バイトの先頭
  uint32_t nextBlock_ = 0;

  // 枠の大きさ（256 バイトがいくつ分か）→ 返されて次に貸せる枠の先頭
  std::unordered_map<uint32_t, std::vector<uint32_t>> freeBlocks_;

  // 256 バイトごとの貸し出し中かどうか（二重返却の検出用）
  std::vector<bool> isAllocated_;

  uint32_t usedBlockCount_ = 0;
  uint32_t peakBlockCount_ = 0;
};
