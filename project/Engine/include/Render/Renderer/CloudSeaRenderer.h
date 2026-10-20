#pragma once
#include "Core/Dx12Core.h"

class Object3dRenderer;

/// <summary>
/// 雲海の描画の準備（ルートシグネチャと PSO）。
/// 平行光源と霧は Object3dRenderer の物をそのまま使う
/// </summary>
class CloudSeaRenderer {

public:
  void Initialize(Dx12Core *dx12Core, Object3dRenderer *object3dRenderer);

  /// <summary>
  /// 描画前の共通処理（PSO・ルートシグネチャ・平行光源・霧を設定する）
  /// </summary>
  void Begin();

  Dx12Core *GetDx12Core() const { return dx12Core_; }

private:
  void CreateRootSignature();
  void CreatePSO();

private:
  Dx12Core *dx12Core_ = nullptr;
  Object3dRenderer *object3dRenderer_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;
};
