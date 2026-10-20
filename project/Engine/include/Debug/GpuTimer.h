#pragma once
#include "Core/Dx12Core.h"
#include <cstdint>
#include <wrl.h>

/// <summary>
/// GPU のタイムスタンプで、Begin から End までの命令にかかった時間を測る。
/// 結果は GPU が終わったあと（次のフレーム）に読む
/// </summary>
class GpuTimer {
public:
  void Initialize(Dx12Core *dx12Core);

  /// <summary>
  /// 測り始めの時刻をコマンドリストに積む
  /// </summary>
  void Begin();
  /// <summary>
  /// 測り終わりの時刻をコマンドリストに積み、読み出し用のバッファへ写す
  /// </summary>
  void End();

  /// <summary>
  /// 前のフレームまでに測った時間（ミリ秒、kSmoothing_ で慣らした値）
  /// </summary>
  float GetMilliseconds();

private:
  // 測った時刻の数（始まりと終わり）
  static constexpr uint32_t kQueryCount_ = 2;
  // 前の値に新しい値を混ぜる割合
  static constexpr float kSmoothing_ = 0.1f;

  Dx12Core *dx12Core_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;
  Microsoft::WRL::ComPtr<ID3D12Resource> readbackResource_;
  // 1 秒あたりのタイムスタンプの刻みの数
  uint64_t frequency_ = 1;
  // End まで積んだが、まだ読んでいない結果があるか
  bool hasPendingResult_ = false;
  float milliseconds_ = 0.0f;
};
