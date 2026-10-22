#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Math/Vector4.h"
#include <d3d12.h>
#include <wrl.h>

class RenderTexture {
public:
  /// <summary>
  /// オフスクリーンテクスチャの生成
  /// </summary>
  /// <param name="dx12Core">DirectX12コア</param>
  /// <param name="srvManager">SRVマネージャー</param>
  /// <param name="width">テクスチャ横幅</param>
  /// <param name="height">テクスチャ縦幅</param>
  /// <param name="clearColor">クリアする色（デフォルトは黒）</param>
  /// <param name="format">テクスチャのフォーマット（デフォルトはR8G8B8A8_UNORM_SRGB）</param>
  void Initialize(Dx12Core* dx12Core, SrvManager* srvManager, uint32_t width, uint32_t height, const Vector4& clearColor = {0.0f, 0.0f, 0.0f, 1.0f}, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

  /// <summary>
  /// 指定したクリアカラーでテクスチャを塗りつぶす
  /// </summary>
  void Clear(Dx12Core* dx12Core);

  /// <summary>
  /// リソースステートを描画対象（RENDER_TARGET）に変更
  /// </summary>
  void TransitionToRenderTarget(Dx12Core* dx12Core);

  /// <summary>
  /// リソースステートを読み込み対象（PIXEL_SHADER_RESOURCE）に変更
  /// </summary>
  void TransitionToShaderResource(Dx12Core* dx12Core);

  // Getter
  uint32_t GetSrvIndex() const { return srv_.Index(); }
  uint32_t GetRtvIndex() const { return rtvIndex_; }
  D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return rtvHandle_; }
  ID3D12Resource* GetResource() const { return resource_.Get(); }

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
  uint32_t rtvIndex_ = 0;
  SrvHandle srv_;
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};
  Vector4 clearColor_;
};
