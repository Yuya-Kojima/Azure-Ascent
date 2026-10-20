#pragma once
#include "Core/Dx12Core.h"
#include "Debug/GpuTimer.h"
#include "Render/CloudSea/CloudNoise.h"
#include "Render/Texture/RenderTexture.h"
#include <cstdint>
#include <memory>

class Object3dRenderer;
class SrvManager;

/// <summary>
/// 雲海の描画の準備（ルートシグネチャと PSO、雲の形に使うノイズ、深度を読むビュー、半分の解像度の雲のテクスチャ）。
/// 平行光源と霧は Object3dRenderer の物をそのまま使う
/// </summary>
class CloudSeaRenderer {

public:
  void Initialize(Dx12Core *dx12Core, Object3dRenderer *object3dRenderer,
                  SrvManager *srvManager);

  /// <summary>
  /// 雲を描く前の処理。深度を読める状態にし、描画先を半分の解像度の雲のテクスチャにして、
  /// PSO・ルートシグネチャ・雲海の値・平行光源・霧・テクスチャを設定する
  /// </summary>
  /// <param name="paramsAddress">雲海の値と形の定数バッファ</param>
  void Begin(D3D12_GPU_VIRTUAL_ADDRESS paramsAddress);

  /// <summary>
  /// 半分の解像度の雲を、深度を見ながら元の大きさの HDR の画面へ重ねる
  /// </summary>
  void Composite();

  /// <summary>
  /// 描画後の処理。深度を書き込める状態に戻す
  /// </summary>
  void End();

  Dx12Core *GetDx12Core() const { return dx12Core_; }

  /// <summary>
  /// 雲海の描画に GPU でかかった時間（ミリ秒。前のフレームまでの値を慣らしたもの）
  /// </summary>
  float GetGpuMilliseconds() { return gpuTimer_.GetMilliseconds(); }

private:
  void CreateRootSignature();
  void CreatePSO();
  /// <summary>
  /// 描く範囲を width × height 画素にする
  /// </summary>
  void SetViewport(uint32_t width, uint32_t height);

private:
  Dx12Core *dx12Core_ = nullptr;
  Object3dRenderer *object3dRenderer_ = nullptr;
  SrvManager *srvManager_ = nullptr;

  CloudNoise noise_;
  GpuTimer gpuTimer_;
  uint32_t depthSrvIndex_ = 0;
  // 半分の解像度で描いた雲（rgb: 雲の光、a: 後ろの光が残る割合）
  std::unique_ptr<RenderTexture> cloudTexture_;
  uint32_t cloudWidth_ = 0;
  uint32_t cloudHeight_ = 0;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
  // 雲を半分の解像度のテクスチャへ描く PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> marchPipelineState_ = nullptr;
  // 雲を元の大きさの画面へ重ねる PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePipelineState_ = nullptr;
};
