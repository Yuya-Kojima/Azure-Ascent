#include "Renderer/CloudSeaRenderer.h"
#include "Debug/Logger.h"
#include "Renderer/Object3dRenderer.h"
#include <cassert>

void CloudSeaRenderer::Initialize(Dx12Core *dx12Core,
                                  Object3dRenderer *object3dRenderer) {
  dx12Core_ = dx12Core;
  object3dRenderer_ = object3dRenderer;
  CreatePSO();
}

void CloudSeaRenderer::CreateRootSignature() {

  HRESULT hr;
  ID3D12Device *device = dx12Core_->GetDevice();

  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  //========================
  // RootParameter
  // 0 : 雲海の値と形（VS・PS b0）
  // 1 : 平行光源（PS b1）
  // 2 : 霧（PS b2）
  // 3 : 雲海の模様のテクスチャ（PS t0）
  //========================
  D3D12_ROOT_PARAMETER rootParameters[4] = {};

  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].Descriptor.ShaderRegister = 0;

  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameters[1].Descriptor.ShaderRegister = 1;

  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameters[2].Descriptor.ShaderRegister = 2;

  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
  descriptorRange[0].BaseShaderRegister = 0;
  descriptorRange[0].NumDescriptors = 1;
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRange;
  rootParameters[3].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange);

  //========================
  // Sampler（模様は繰り返して貼る）
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
  // InputLayout
  // 頂点は網の中心からの水平の位置（x, z）だけを持つ
  //========================
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[1] = {};
  inputElementDescs[0].SemanticName = "POSITION";
  inputElementDescs[0].SemanticIndex = 0;
  inputElementDescs[0].Format = DXGI_FORMAT_R32G32_FLOAT;
  inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

  D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
  inputLayoutDesc.pInputElementDescs = inputElementDescs;
  inputLayoutDesc.NumElements = _countof(inputElementDescs);

  //========================
  // Blend（不透明）
  //========================
  D3D12_BLEND_DESC blendDesc{};
  blendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;
  blendDesc.RenderTarget[0].BlendEnable = false;

  //========================
  // Rasterizer（両面を描く）
  //========================
  D3D12_RASTERIZER_DESC rasterizerDesc{};
  rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
  rasterizerDesc.FrontCounterClockwise = FALSE;
  rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

  //========================
  // Shader
  //========================
  IDxcBlob *vertexShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudSea.VS.hlsl", L"vs_6_0");
  assert(vertexShaderBlob != nullptr);

  IDxcBlob *pixelShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/CloudSea.PS.hlsl", L"ps_6_0");
  assert(pixelShaderBlob != nullptr);

  //========================
  // DepthStencil（深度を比べて書く）
  //========================
  D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
  depthStencilDesc.DepthEnable = true;
  depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  //========================
  // PSO
  //========================
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = rootSignature_.Get();
  psoDesc.InputLayout = inputLayoutDesc;

  psoDesc.VS = {vertexShaderBlob->GetBufferPointer(),
                vertexShaderBlob->GetBufferSize()};

  psoDesc.PS = {pixelShaderBlob->GetBufferPointer(),
                pixelShaderBlob->GetBufferSize()};

  psoDesc.BlendState = blendDesc;
  psoDesc.RasterizerState = rasterizerDesc;
  psoDesc.DepthStencilState = depthStencilDesc;
  psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.SampleDesc.Count = 1;
  psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

  hr = device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&graphicsPipelineState_));
  assert(SUCCEEDED(hr));
}

void CloudSeaRenderer::Begin() {
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  commandList->SetGraphicsRootSignature(rootSignature_.Get());
  commandList->SetPipelineState(graphicsPipelineState_.Get());
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // 平行光源と霧はほかの物と同じ物を使う
  commandList->SetGraphicsRootConstantBufferView(
      1, object3dRenderer_->GetDirectionalLightResource()->GetGPUVirtualAddress());
  commandList->SetGraphicsRootConstantBufferView(
      2, object3dRenderer_->GetFogResource()->GetGPUVirtualAddress());
}
