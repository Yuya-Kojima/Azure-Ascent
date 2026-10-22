#include "Renderer/CloudSeaRenderer.h"
#include "Core/SrvManager.h"
#include "Debug/Logger.h"
#include "Renderer/Object3dRenderer.h"
#include <cassert>

void CloudSeaRenderer::Initialize(Dx12Core *dx12Core,
                                  Object3dRenderer *object3dRenderer,
                                  SrvManager *srvManager) {
  dx12Core_ = dx12Core;
  object3dRenderer_ = object3dRenderer;
  srvManager_ = srvManager;

  noise_.Initialize(dx12Core_, srvManager_);
  gpuTimer_.Initialize(dx12Core_);

  depthSrv_ = srvManager_->Allocate();
  srvManager_->CreateSRVforDepth(depthSrv_.Index(),
                                 dx12Core_->GetDepthStencilResource());

  // 半分の解像度の雲のテクスチャ（何もない所は 雲の光 0・後ろが全部残る）
  cloudWidth_ = WindowSystem::kClientWidth / 2;
  cloudHeight_ = WindowSystem::kClientHeight / 2;
  cloudTexture_ = std::make_unique<RenderTexture>();
  cloudTexture_->Initialize(dx12Core_, srvManager_, cloudWidth_, cloudHeight_,
                            {0.0f, 0.0f, 0.0f, 1.0f},
                            DXGI_FORMAT_R16G16B16A16_FLOAT);
  cloudTexture_->TransitionToShaderResource(dx12Core_);

  CreatePSO();
}

void CloudSeaRenderer::CreateRootSignature() {

  HRESULT hr;
  ID3D12Device *device = dx12Core_->GetDevice();

  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  //========================
  // RootParameter
  // 0 : 雲海の値と形（PS b0）
  // 1 : 平行光源（PS b1）
  // 2 : 霧（PS b2）
  // 3 : 深度（PS t0）
  // 4 : 大きなノイズ（PS t1）
  // 5 : 細かいノイズ（PS t2）
  // 6 : 上の面の起伏のノイズ（PS t3）
  // 7 : 半分の解像度で描いた雲（PS t4。重ねるときに使う）
  //========================
  D3D12_ROOT_PARAMETER rootParameters[8] = {};

  for (UINT i = 0; i < 3; ++i) {
    rootParameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[i].Descriptor.ShaderRegister = i;
  }

  D3D12_DESCRIPTOR_RANGE descriptorRanges[5] = {};
  for (UINT i = 0; i < _countof(descriptorRanges); ++i) {
    descriptorRanges[i].BaseShaderRegister = i;
    descriptorRanges[i].NumDescriptors = 1;
    descriptorRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[i].OffsetInDescriptorsFromTableStart =
        D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER &parameter = rootParameters[3 + i];
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    parameter.DescriptorTable.pDescriptorRanges = &descriptorRanges[i];
    parameter.DescriptorTable.NumDescriptorRanges = 1;
  }

  //========================
  // Sampler（ノイズは繰り返して貼る）
  //========================
  D3D12_STATIC_SAMPLER_DESC staticSampler{};
  staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
  staticSampler.ShaderRegister = 0;
  staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);
  descriptionRootSignature.pStaticSamplers = &staticSampler;
  descriptionRootSignature.NumStaticSamplers = 1;

  Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

  hr = D3D12SerializeRootSignature(
      &descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1,
      signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());

  if (FAILED(hr)) {
    if (errorBlob) {
      Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    }
    assert(false);
  }

  hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                   signatureBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&rootSignature_));
  assert(SUCCEEDED(hr));
}

void CloudSeaRenderer::CreatePSO() {

  HRESULT hr;
  ID3D12Device *device = dx12Core_->GetDevice();

  CreateRootSignature();

  //========================
  // Blend（描くときはそのまま書く。重ねるときは 雲の光 ＋ 後ろの物の光 × 残る割合）
  //========================
  D3D12_BLEND_DESC marchBlendDesc{};
  marchBlendDesc.RenderTarget[0].BlendEnable = false;
  marchBlendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  D3D12_BLEND_DESC blendDesc{};
  blendDesc.RenderTarget[0].BlendEnable = true;
  blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
  blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_ALPHA;
  blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
  blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  //========================
  // Rasterizer
  //========================
  D3D12_RASTERIZER_DESC rasterizerDesc{};
  rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
  rasterizerDesc.FrontCounterClockwise = FALSE;
  rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

  //========================
  // Shader（画面全体の三角形）
  //========================
  IDxcBlob *vertexShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Fullscreen.VS.hlsl", L"vs_6_0");
  assert(vertexShaderBlob != nullptr);

  IDxcBlob *marchShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudSea.March.PS.hlsl", L"ps_6_0");
  assert(marchShaderBlob != nullptr);

  IDxcBlob *compositeShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudSea.Composite.PS.hlsl", L"ps_6_0");
  assert(compositeShaderBlob != nullptr);

  //========================
  // DepthStencil（深度はシェーダーで読むので使わない）
  //========================
  D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
  depthStencilDesc.DepthEnable = false;

  //========================
  // PSO
  //========================
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = rootSignature_.Get();

  psoDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize()};

  psoDesc.PS = {marchShaderBlob->GetBufferPointer(),
                marchShaderBlob->GetBufferSize()};

  psoDesc.BlendState = marchBlendDesc;
  psoDesc.RasterizerState = rasterizerDesc;
  psoDesc.DepthStencilState = depthStencilDesc;
  psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.SampleDesc.Count = 1;
  psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

  hr = device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&marchPipelineState_));
  assert(SUCCEEDED(hr));

  psoDesc.PS = {compositeShaderBlob->GetBufferPointer(),
                compositeShaderBlob->GetBufferSize()};
  psoDesc.BlendState = blendDesc;
  hr = device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&compositePipelineState_));
  assert(SUCCEEDED(hr));
}

void CloudSeaRenderer::SetViewport(uint32_t width, uint32_t height) {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();
  D3D12_VIEWPORT viewport{};
  viewport.Width = float(width);
  viewport.Height = float(height);
  viewport.MaxDepth = 1.0f;
  D3D12_RECT scissor{0, 0, LONG(width), LONG(height)};
  commandList->RSSetViewports(1, &viewport);
  commandList->RSSetScissorRects(1, &scissor);
}

void CloudSeaRenderer::Begin(D3D12_GPU_VIRTUAL_ADDRESS paramsAddress) {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  // 最初の 1 回だけ、雲の形に使うノイズを作る
  noise_.GenerateIfNeeded();

  // 深度を読めるようにする（描画先は HDR の画面だけになる）
  dx12Core_->BeginSceneDepthRead();

  // ここから End までの時間を測る（前の物の描画が終わるのを待つ切り替えのあとから）
  gpuTimer_.Begin();

  // 描画先を半分の解像度の雲のテクスチャにする
  cloudTexture_->TransitionToRenderTarget(dx12Core_);
  D3D12_CPU_DESCRIPTOR_HANDLE cloudRtv = cloudTexture_->GetRtvHandle();
  commandList->OMSetRenderTargets(1, &cloudRtv, false, nullptr);
  SetViewport(cloudWidth_, cloudHeight_);

  commandList->SetGraphicsRootSignature(rootSignature_.Get());
  commandList->SetPipelineState(marchPipelineState_.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  commandList->SetGraphicsRootConstantBufferView(0, paramsAddress);
  // 平行光源と霧はほかの物と同じ物を使う
  commandList->SetGraphicsRootConstantBufferView(
      1, object3dRenderer_->GetDirectionalLightResource()->GetGPUVirtualAddress());
  commandList->SetGraphicsRootConstantBufferView(
      2, object3dRenderer_->GetFogResource()->GetGPUVirtualAddress());

  commandList->SetGraphicsRootDescriptorTable(
      3, srvManager_->GetGPUDescriptorHandle(depthSrv_.Index()));
  commandList->SetGraphicsRootDescriptorTable(4, noise_.GetBaseSrvHandle());
  commandList->SetGraphicsRootDescriptorTable(5, noise_.GetDetailSrvHandle());
  commandList->SetGraphicsRootDescriptorTable(6, noise_.GetTopSrvHandle());
}

void CloudSeaRenderer::Composite() {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  // 雲のテクスチャを読む状態にし、描画先を元の大きさの 3D の画面に戻す
  cloudTexture_->TransitionToShaderResource(dx12Core_);
  D3D12_CPU_DESCRIPTOR_HANDLE sceneRtv = dx12Core_->GetSceneRenderTarget();
  commandList->OMSetRenderTargets(1, &sceneRtv, false, nullptr);
  SetViewport(WindowSystem::kClientWidth, WindowSystem::kClientHeight);

  commandList->SetPipelineState(compositePipelineState_.Get());
  commandList->SetGraphicsRootDescriptorTable(
      7, srvManager_->GetGPUDescriptorHandle(cloudTexture_->GetSrvIndex()));
  commandList->DrawInstanced(3, 1, 0, 0);
}

void CloudSeaRenderer::End() {
  gpuTimer_.End();
  dx12Core_->EndSceneDepthRead();
}
