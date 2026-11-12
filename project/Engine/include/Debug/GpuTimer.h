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
  /// 測り始めの時刻を、指定した命令の列に積む（End と違う列に積むときに使う）
  /// </summary>
  void Begin(ID3D12GraphicsCommandList *commandList);
  /// <summary>
  /// 測り終わりの時刻をコマンドリストに積み、読み出し用のバッファへ写す
  /// </summary>
  void End();

  /// <summary>
  /// 前のフレームまでに測った時間（ミリ秒、kSmoothing_ で慣らした値）
  /// </summary>
  float GetMilliseconds();

  /// <summary>
  /// 直近にまとめ終わった kWindowSampleCount_ 回分の時間（ミリ秒）
  /// </summary>
  struct WindowStats {
    float average = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    // まとめ終わった区切りがまだ無いとき false
    bool isValid = false;
  };

  /// <summary>
  /// 直近にまとめ終わった区切りの平均・最小・最大（区切りごとに更新される）
  /// </summary>
  WindowStats GetWindowStats();

  /// <summary>
  /// 1 つの区切りにまとめる回数
  /// </summary>
  static constexpr uint32_t GetWindowSampleCount() { return kWindowSampleCount_; }

private:
  /// <summary>
  /// 前のフレームで積んだ結果を読み、慣らした値と区切りの集計に入れる
  /// </summary>
  void ReadPendingResult();

  // 1 つの区切りにまとめる回数（60 fps で約 2 秒）
  static constexpr uint32_t kWindowSampleCount_ = 120;

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

  // 集計中の区切りの合計・最小・最大と回数
  double windowSum_ = 0.0;
  float windowMin_ = 0.0f;
  float windowMax_ = 0.0f;
  uint32_t windowCount_ = 0;
  // 直近にまとめ終わった区切りの結果
  WindowStats lastWindowStats_;
};
