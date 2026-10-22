#include "Render/Texture/RenderTexture.h"
#include <cassert>

void RenderTexture::Initialize(Dx12Core *dx12Core, SrvManager *srvManager,
                               uint32_t width, uint32_t height,
                               const Vector4 &clearColor, DXGI_FORMAT format) {
  clearColor_ = clearColor;
  
  // テクスチャリソースの生成
  resource_ = CreateRenderTextureResource(dx12Core->GetDevice(), width, height,
                                          format, clearColor_);

  // 生成直後は RENDER_TARGET なので名簿に登録
  dx12Core->RegisterResourceState(resource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

  // RTVの生成
  rtvIndex_ = dx12Core->AllocateRTV();
  rtvHandle_ = dx12Core->GetRtvCpuDescriptorHandle(rtvIndex_);

  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
  rtvDesc.Format = format;
  rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

  dx12Core->GetDevice()->CreateRenderTargetView(resource_.Get(), &rtvDesc,
                                                rtvHandle_);

  // SRVの生成
  srv_ = srvManager->Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = format;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;

  dx12Core->GetDevice()->CreateShaderResourceView(
      resource_.Get(), &srvDesc, srvManager->GetCPUDescriptorHandle(srv_.Index()));
}

void RenderTexture::Clear(Dx12Core *dx12Core) {
  auto commandList = dx12Core->GetCommandList();

  // 現在のステートが RENDER_TARGET じゃなければ変更する
  TransitionToRenderTarget(dx12Core);

  // クリア実行
  float clearColorArr[4] = {clearColor_.x, clearColor_.y, clearColor_.z,
                            clearColor_.w};
  commandList->ClearRenderTargetView(rtvHandle_, clearColorArr, 0, nullptr);
}

void RenderTexture::TransitionToRenderTarget(Dx12Core *dx12Core) {
  dx12Core->TransitionResource(resource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void RenderTexture::TransitionToShaderResource(Dx12Core *dx12Core) {
  dx12Core->TransitionResource(resource_.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}
