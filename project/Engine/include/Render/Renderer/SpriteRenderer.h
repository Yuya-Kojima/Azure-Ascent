#pragma once
#include "Core/Dx12Core.h"

class SpriteRenderer {

public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(Dx12Core *dx12Core);

  // Dx12Core *GetDx12Core() const { return dx12Core_; }

  /// <summary>
  /// 通常描画設定
  /// </summary>
  void Begin();

  /// <summary>
  /// UIEffect描画設定
  /// </summary>
  void BeginUIEffect();

private:
  Dx12Core *dx12Core_ = nullptr;

public:
  Dx12Core *GetDx12Core() const { return dx12Core_; }

private:
  ID3D12Device *device_ = nullptr;

  // ルートシグネチャ
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;

  // PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipeLineState_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> uiEffectPipeLineState_ = nullptr;

  /// <summary>
  /// ルートシグネチャの生成
  /// </summary>
  void CreateSpriteRootSignature();

  /// <summary>
  /// PSOの生成
  /// </summary>
  void CreateSpritePSO();
};
