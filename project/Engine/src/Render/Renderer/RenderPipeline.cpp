#include "Render/Renderer/RenderPipeline.h"
#include "Core/WindowSystem.h"

void RenderPipeline::Initialize(Dx12Core* dx12Core, SrvManager* srvManager) {
  const Vector4 kRenderTargetClearValue{ 0.1f, 0.25f, 0.5f, 1.0f }; // 背景色をDx12Coreと同じ青色に統一

  // HDRキャンバス (mainRenderTexture_) の作成
  mainRenderTexture_ = std::make_unique<RenderTexture>();
  mainRenderTexture_->Initialize(dx12Core, srvManager, WindowSystem::kClientWidth, WindowSystem::kClientHeight, kRenderTargetClearValue, DXGI_FORMAT_R16G16B16A16_FLOAT);

  // エディタ画面表示用テクスチャの作成 (LDR)
  editorGameViewTexture_ = std::make_unique<RenderTexture>();
  editorGameViewTexture_->Initialize(dx12Core, srvManager, WindowSystem::kClientWidth, WindowSystem::kClientHeight, kRenderTargetClearValue, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

  // Bloomの初期化
  bloom_ = std::make_unique<Bloom>();
  bloom_->Initialize(dx12Core, srvManager, WindowSystem::kClientWidth, WindowSystem::kClientHeight);

  // モーションブラー用のHistoryテクスチャ初期化
  for (int i = 0; i < 2; ++i) {
    historyTextures_[i] = std::make_unique<RenderTexture>();
    historyTextures_[i]->Initialize(dx12Core, srvManager, WindowSystem::kClientWidth, WindowSystem::kClientHeight, kRenderTargetClearValue, DXGI_FORMAT_R16G16B16A16_FLOAT);
    historyTextures_[i]->Clear(dx12Core);
    // SRVに遷移しておく
    historyTextures_[i]->TransitionToShaderResource(dx12Core);
  }

  // DepthTextureのSRV作成
  depthTextureSrv_ = srvManager->Allocate();
  srvManager->CreateSRVforDepth(depthTextureSrv_.Index(), dx12Core->GetDepthStencilResource());
}

void RenderPipeline::Begin3DPass(Dx12Core* dx12Core) {
  // メインのオフスクリーンテクスチャ（HDRキャンバス）を描画対象に設定
  mainRenderTexture_->TransitionToRenderTarget(dx12Core);
  
  auto commandList = dx12Core->GetCommandList();
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mainRenderTexture_->GetRtvHandle();
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dx12Core->GetDsvCpuDescriptorHandle();
  
  commandList->OMSetRenderTargets(1, &rtvHandle, false, &dsvHandle);
  // 3D の途中で深度を読む描画（雲海）が、描画先を戻せるように覚えておく
  dx12Core->SetSceneRenderTarget(rtvHandle);
  
  // 画面クリア
  mainRenderTexture_->Clear(dx12Core);
}

void RenderPipeline::DrawPostProcess(Dx12Core* dx12Core, SrvManager* srvManager, PostProcess* currentScenePostProcess) {
  // 抽出パスで読むために ShaderResource へ遷移
  mainRenderTexture_->TransitionToShaderResource(dx12Core);
  uint32_t bloomSrvIndex = 0;

  if (currentScenePostProcess) {
    bloom_->SetThreshold(currentScenePostProcess->GetBloomThreshold());
    bloom_->SetSigma(currentScenePostProcess->GetBloomSigma());
    
    if (currentScenePostProcess->GetUseBloom()) {
      bloomSrvIndex = bloom_->Draw(dx12Core, srvManager, mainRenderTexture_->GetSrvIndex());
    }
  } else {
    bloomSrvIndex = bloom_->Draw(dx12Core, srvManager, mainRenderTexture_->GetSrvIndex());
  }

  // スワップチェーン（メイン画面）へ描画先を切り替える準備（バックバッファクリア等）
  dx12Core->PreDrawImGui();

  // エディタモードの場合はエディタ用テクスチャをターゲットにする
  if (isEditorMode_) {
    editorGameViewTexture_->TransitionToRenderTarget(dx12Core);
    editorGameViewTexture_->Clear(dx12Core);
  }

  if (currentScenePostProcess) {
    currentScenePostProcess->SetBloomSrvIndex(bloomSrvIndex);
    
    uint32_t prevHistoryIndex = (currentHistoryIndex_ + 1) % 2;
    currentScenePostProcess->SetHistorySrvIndex(historyTextures_[prevHistoryIndex]->GetSrvIndex());
    
    historyTextures_[currentHistoryIndex_]->TransitionToRenderTarget(dx12Core);
    
    // MRTの設定：[0] バックバッファ(またはエディタテクスチャ), [1] 次フレーム用History
    D3D12_CPU_DESCRIPTOR_HANDLE targetRtv = isEditorMode_ ? editorGameViewTexture_->GetRtvHandle() : dx12Core->GetBackBufferRtvHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2] = { targetRtv, historyTextures_[currentHistoryIndex_]->GetRtvHandle() };
    
    auto commandList = dx12Core->GetCommandList();
    commandList->OMSetRenderTargets(2, rtvHandles, false, nullptr);
    
    // DepthBufferをSRVとして読み取るために状態遷移
    dx12Core->TransitionResource(dx12Core->GetDepthStencilResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    currentScenePostProcess->Draw(mainRenderTexture_->GetSrvIndex(), depthTextureSrv_.Index(), srvManager);
    
    // DepthBufferを元のDEPTH_WRITEに戻す
    dx12Core->TransitionResource(dx12Core->GetDepthStencilResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    
    historyTextures_[currentHistoryIndex_]->TransitionToShaderResource(dx12Core);
    currentHistoryIndex_ = prevHistoryIndex;
  }
  
  // 以降のオーバーレイ描画のためにターゲットを設定
  {
      D3D12_CPU_DESCRIPTOR_HANDLE overlayTarget = isEditorMode_ ? editorGameViewTexture_->GetRtvHandle() : dx12Core->GetBackBufferRtvHandle();
      auto commandList = dx12Core->GetCommandList();
      commandList->OMSetRenderTargets(1, &overlayTarget, false, nullptr);
  }
}

uint32_t RenderPipeline::GetEditorGameViewSrvIndex() const {
  return editorGameViewTexture_->GetSrvIndex();
}

void RenderPipeline::EndEditorGameViewPass(Dx12Core* dx12Core) {
  if (isEditorMode_) {
    // SRVに遷移させてImGuiで読み取れるようにする
    editorGameViewTexture_->TransitionToShaderResource(dx12Core);

    // 描画ターゲットをバックバッファに戻す（この後ImGui自体を描画するため）
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferHandle = dx12Core->GetBackBufferRtvHandle();
    auto commandList = dx12Core->GetCommandList();
    commandList->OMSetRenderTargets(1, &backBufferHandle, false, nullptr);
  }
}
