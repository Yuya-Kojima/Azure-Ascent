#include "Particle/ParticleManager.h"
#include "Particle/IParticleEmitter.h"
#include "Core/SrvManager.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Render/Primitive/Cylinder.h"
#include "Render/Primitive/Ring.h"
#include "Texture/TextureManager.h"
#include <cassert>
#include <numbers>

ParticleManager *ParticleManager::GetInstance() {
  static ParticleManager instance;
  return &instance;
}

void ParticleManager::Initialize(Dx12Core *dx12Core, SrvManager *srvManager) {

  // ポインタを受け取ってメンバ変数に記録
  dx12Core_ = dx12Core;

  srvManager_ = srvManager;

  // GPUパーティクル用の初期化（Compute PSOとリソースの準備）
  CreateComputePipeline();
  CreateEmitComputePipeline();
  CreateUpdateComputePipeline();
  InitializeSharedResources();

  // パイプライン生成
  CreatePSO();
}

// Group methods removed

void ParticleManager::Finalize() {

  // GPUリソース類を解放
  graphicsPipeLineState_.Reset();
  alphaPipelineState_.Reset();
  rootSignature_.Reset();

  computeRootSignature_.Reset();
  initializeComputePipelineState_.Reset();

  emitComputeRootSignature_.Reset();
  emitComputePipelineState_.Reset();

  updateComputeRootSignature_.Reset();
  updateComputePipelineState_.Reset();

  if (perViewResource_) {
      perViewResource_->Unmap(0, nullptr);
      perViewResource_.Reset();
  }
  if (perFrameResource_) {
      perFrameResource_->Unmap(0, nullptr);
      perFrameResource_.Reset();
  }

  dx12Core_ = nullptr;
  srvManager_ = nullptr;
}

// Old clear and emit methods removed
void ParticleManager::CreateRootSignature() {

  HRESULT hr;

  // RootSignatureを作成
  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  // Particle用のRootParameter作成。
  D3D12_ROOT_PARAMETER rootParameters[4] = {};
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameters[0].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_ALL;                 // 全シェーダーで使う
  rootParameters[0].Descriptor.ShaderRegister = 1; // レジスタ番号1(b1)とバインド

  D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
  descriptorRangeForInstancing[0].BaseShaderRegister = 0; // 0から始まる
  descriptorRangeForInstancing[0].NumDescriptors = 1;     // 数は一つ
  descriptorRangeForInstancing[0].RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
  descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
  rootParameters[1].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
  rootParameters[1].DescriptorTable.pDescriptorRanges =
      descriptorRangeForInstancing; // Tableの中身の配列を指定
  rootParameters[1].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRangeForInstancing); // Tableで利用する数

  D3D12_DESCRIPTOR_RANGE descriptorRangeParticle[1] = {};
  descriptorRangeParticle[0].BaseShaderRegister = 0; // ０から始まる
  descriptorRangeParticle[0].NumDescriptors = 1;     // 数は1つ
  descriptorRangeParticle[0].RangeType =
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
  descriptorRangeParticle[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

  rootParameters[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
  rootParameters[2].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
  rootParameters[2].DescriptorTable.pDescriptorRanges =
      descriptorRangeParticle; // Tableの中身の配列を指定
  rootParameters[2].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRangeParticle); // Tableで利用する数

  // VSとPS用のPerView(CBV)
  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[3].Descriptor.ShaderRegister = 0; // レジスタ番号b0

  D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
  staticSamplers[0].Filter =
      D3D12_FILTER_MIN_MAG_MIP_LINEAR; // バイリニアフィルタ
  staticSamplers[0].AddressU =
      D3D12_TEXTURE_ADDRESS_MODE_WRAP; // 0~1の範囲外をリピート
  staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER; // 比較しない
  staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
  staticSamplers[0].ShaderRegister = 0; // レジスタ番号0
  staticSamplers[0].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL; // pixelShaderを使う

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);
  descriptionRootSignature.pStaticSamplers = staticSamplers;
  descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

  ID3DBlob *signatureBlobPart = nullptr;
  ID3DBlob *errorBlobPart = nullptr;
  hr = D3D12SerializeRootSignature(
      &descriptionRootSignature, // Particle 用記述子を渡す
      D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlobPart, &errorBlobPart);

  if (FAILED(hr)) {
    Logger::Log(reinterpret_cast<char *>(errorBlobPart->GetBufferPointer()));
    assert(false);
  }

  rootSignature_ = nullptr;
  hr = dx12Core_->GetDevice()->CreateRootSignature(
      0, signatureBlobPart->GetBufferPointer(),
      signatureBlobPart->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));

  assert(SUCCEEDED(hr));
}

void ParticleManager::CreatePSO() {

  HRESULT hr;

  CreateRootSignature();

  // inputLayout
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
  inputElementDescs[0].SemanticName = "POSITION";
  inputElementDescs[0].SemanticIndex = 0;
  inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

  inputElementDescs[1].SemanticName = "TEXCOORD";
  inputElementDescs[1].SemanticIndex = 0;
  inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
  inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

  inputElementDescs[2].SemanticName = "NORMAL";
  inputElementDescs[2].SemanticIndex = 0;
  inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
  inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

  D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
  inputLayoutDesc.pInputElementDescs = inputElementDescs;
  inputLayoutDesc.NumElements = _countof(inputElementDescs);

  // BlendStateの設定
  D3D12_BLEND_DESC blendDesc{};

  // すべての色要素を書き込む
  blendDesc.RenderTarget[0].RenderTargetWriteMask =
      D3D12_COLOR_WRITE_ENABLE_ALL;

  blendDesc.RenderTarget[0].BlendEnable = true; // ブレンドを有効化

  blendDesc.RenderTarget[0].SrcBlend =
      D3D12_BLEND_SRC_ALPHA; // 元画像の透明度を使う

  blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; // 合成方法　(加算)

  blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;

  blendDesc.RenderTarget[0].SrcBlendAlpha =
      D3D12_BLEND_ONE; // 元画像の透明度をブレンドに反映

  blendDesc.RenderTarget[0].BlendOpAlpha =
      D3D12_BLEND_OP_ADD; // 上二つを合成(加算)

  blendDesc.RenderTarget[0].DestBlendAlpha =
      D3D12_BLEND_ZERO; // 背景の透明度は無視

  // RasterizerStateの設定
  D3D12_RASTERIZER_DESC rasterizerDesc{};

  // 裏面(時計回り)を表示しない
  rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;

  // 三角形の中を塗りつぶす
  rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

  // shaderをcompileする
  IDxcBlob *particleVertexShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Particle.VS.hlsl", L"vs_6_0");

  IDxcBlob *particlePixelShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Particle.PS.hlsl", L"ps_6_0");

  // DepthStencilStateの設定
  D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

  // Depthの機能を有効化する
  depthStencilDesc.DepthEnable = true;

  // 書き込み
  depthStencilDesc.DepthWriteMask =
      D3D12_DEPTH_WRITE_MASK_ZERO; // Depthの書き込みを行わない

  // 比較関数はLessEqual。近ければ描画される
  depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  // particle用PSO
  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipeLineStateDesc{};
  graphicsPipeLineStateDesc.pRootSignature =
      rootSignature_.Get();                                // RootSignature
  graphicsPipeLineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
  graphicsPipeLineStateDesc.VS = {
      particleVertexShaderBlob->GetBufferPointer(),
      particleVertexShaderBlob->GetBufferSize(),
  }; // VertexShader
  graphicsPipeLineStateDesc.PS = {
      particlePixelShaderBlob->GetBufferPointer(),
      particlePixelShaderBlob->GetBufferSize()};              // PixelShader
  graphicsPipeLineStateDesc.BlendState = blendDesc;           // BlendState
  graphicsPipeLineStateDesc.RasterizerState = rasterizerDesc; // RasterizerState
  graphicsPipeLineStateDesc.DepthStencilState = depthStencilDesc;
  graphicsPipeLineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

  // 書き込むRTVの情報
  graphicsPipeLineStateDesc.NumRenderTargets = 1;
  graphicsPipeLineStateDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;

  // 利用するトポロジ(形状)のタイプ。三角形
  graphicsPipeLineStateDesc.PrimitiveTopologyType =
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

  // どのように画面に色を打ち込むかの設定
  graphicsPipeLineStateDesc.SampleDesc.Count = 1;
  graphicsPipeLineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

  // 実際に生成
  graphicsPipeLineState_ = nullptr;
  hr = dx12Core_->GetDevice()->CreateGraphicsPipelineState(
      &graphicsPipeLineStateDesc, IID_PPV_ARGS(&graphicsPipeLineState_));
  assert(SUCCEEDED(hr));

  // 半透明合成用PSO：背景を (1 - α) の割合で残す
  graphicsPipeLineStateDesc.BlendState.RenderTarget[0].DestBlend =
      D3D12_BLEND_INV_SRC_ALPHA;
  alphaPipelineState_ = nullptr;
  hr = dx12Core_->GetDevice()->CreateGraphicsPipelineState(
      &graphicsPipeLineStateDesc, IID_PPV_ARGS(&alphaPipelineState_));
  assert(SUCCEEDED(hr));
}

void ParticleManager::CreateComputePipeline() {
  HRESULT hr;

  // Compute用のRootSignature作成
  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  D3D12_ROOT_PARAMETER rootParameters[3] = {};
  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};

  // UAV用のRange (u0)
  descriptorRange[0].BaseShaderRegister = 0;
  descriptorRange[0].NumDescriptors = 1;
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
  rootParameters[0].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange);

  // フリーリストインデックス UAV (RootUAV, u1)
  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[1].Descriptor.ShaderRegister = 1; // u1

  // フリーリスト UAV (RootUAV, u2)
  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[2].Descriptor.ShaderRegister = 2; // u2

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);

  ID3DBlob *signatureBlob = nullptr;
  ID3DBlob *errorBlob = nullptr;
  hr = D3D12SerializeRootSignature(&descriptionRootSignature,
                                   D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob,
                                   &errorBlob);
  if (FAILED(hr)) {
    Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    assert(false);
  }

  computeRootSignature_ = nullptr;
  hr = dx12Core_->GetDevice()->CreateRootSignature(
      0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
      IID_PPV_ARGS(&computeRootSignature_));
  assert(SUCCEEDED(hr));

  // ComputeShaderのコンパイルとPSO作成
  IDxcBlob *computeShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/InitializeParticle.CS.hlsl", L"cs_6_0");

  D3D12_COMPUTE_PIPELINE_STATE_DESC computePipeLineStateDesc{};
  computePipeLineStateDesc.pRootSignature = computeRootSignature_.Get();
  computePipeLineStateDesc.CS = {computeShaderBlob->GetBufferPointer(),
                                 computeShaderBlob->GetBufferSize()};

  initializeComputePipelineState_ = nullptr;
  hr = dx12Core_->GetDevice()->CreateComputePipelineState(
      &computePipeLineStateDesc,
      IID_PPV_ARGS(&initializeComputePipelineState_));
  assert(SUCCEEDED(hr));
}

void ParticleManager::InitializeSharedResources() {

  // 3. PerView用のResourceを確保 (CBV用)
  perViewResource_ = dx12Core_->CreateBufferResource(sizeof(PerView));
  perViewResource_->Map(0, nullptr, reinterpret_cast<void **>(&perViewData_));
  perViewData_->viewProjection = MakeIdentity4x4();
  perViewData_->billboardMatrix = MakeIdentity4x4();

	// 4. EmitterSphere用のResourceは各 GPUParticleEmitter に移動したため削除

  // 4.5. PerFrame用のResourceを確保 (CBV用)
  perFrameResource_ = dx12Core_->CreateBufferResource(sizeof(PerFrame));
  perFrameResource_->Map(0, nullptr, reinterpret_cast<void **>(&perFrameData_));
  perFrameData_->time = 0.0f;
  perFrameData_->deltaTime = 0.0f;

}

void ParticleManager::InitializeEmitter(IParticleEmitter* emitter, bool isFirstInit) {
  auto commandList = dx12Core_->GetCommandList();
  srvManager_->PreDraw();

  // リソースバリア: 計算用（UAV）に状態遷移

  dx12Core_->TransitionResource(emitter->GetGPUParticleResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  if (isFirstInit) {
      dx12Core_->TransitionResource(emitter->GetFreeListIndexResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      dx12Core_->TransitionResource(emitter->GetFreeListResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  commandList->SetComputeRootSignature(computeRootSignature_.Get());
  commandList->SetPipelineState(initializeComputePipelineState_.Get());

  commandList->SetComputeRootDescriptorTable(
      0, srvManager_->GetGPUDescriptorHandle(emitter->GetGPUParticleUavIndex()));
  commandList->SetComputeRootUnorderedAccessView(
      1, emitter->GetFreeListIndexResource()->GetGPUVirtualAddress());
  commandList->SetComputeRootUnorderedAccessView(
      2, emitter->GetFreeListResource()->GetGPUVirtualAddress());

  commandList->Dispatch((kMaxParticles + 1023) / 1024, 1, 1);

  // ResourceBarrier: UNORDERED_ACCESS -> NON_PIXEL_SHADER_RESOURCE
  dx12Core_->TransitionResource(emitter->GetGPUParticleResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void ParticleManager::Update() {
  if (perFrameData_) {
    const float kDeltaTime = 1.0f / 60.0f;
    perFrameData_->deltaTime = kDeltaTime;
    perFrameData_->time += kDeltaTime;
  }
}

void ParticleManager::CreateEmitComputePipeline() {
  HRESULT hr = S_FALSE;

  // 1. RootSignature の作成
  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
  descriptorRange[0].BaseShaderRegister = 0; // u0 (gParticles)
  descriptorRange[0].NumDescriptors = 1;
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER rootParameters[5] = {};
  // パラメータ0: UAV (DescriptorTable)
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
  rootParameters[0].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange);

  // パラメータ1: EmitterSphere (CBV)
  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[1].Descriptor.ShaderRegister = 0; // b0

  // パラメータ2: PerFrame (CBV)
  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[2].Descriptor.ShaderRegister = 1; // b1

  // パラメータ3: フリーリストインデックス (RootUAV)
  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[3].Descriptor.ShaderRegister = 1; // u1

  // パラメータ4: フリーリスト (RootUAV)
  rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[4].Descriptor.ShaderRegister = 2; // u2

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);

  Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
  hr = D3D12SerializeRootSignature(&descriptionRootSignature,
                                   D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob,
                                   &errorBlob);
  if (FAILED(hr)) {
    Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    assert(false);
  }
  hr = dx12Core_->GetDevice()->CreateRootSignature(
      0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
      IID_PPV_ARGS(&emitComputeRootSignature_));
  assert(SUCCEEDED(hr));

  // 2. Compute Shader のコンパイル
  Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob = nullptr;
  computeShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/EmitParticle.CS.hlsl", L"cs_6_0");
  assert(computeShaderBlob != nullptr);

  // 3. PSO の作成
  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = emitComputeRootSignature_.Get();
  psoDesc.CS = {computeShaderBlob->GetBufferPointer(),
                computeShaderBlob->GetBufferSize()};

  hr = dx12Core_->GetDevice()->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&emitComputePipelineState_));
  assert(SUCCEEDED(hr));
}

void ParticleManager::CreateUpdateComputePipeline() {
  HRESULT hr;

  // Update用のRootSignature作成
  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
  // UAV用のRange (u0)
  descriptorRange[0].BaseShaderRegister = 0;
  descriptorRange[0].NumDescriptors = 1;
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER rootParameters[4] = {};
  // パラメータ0: UAV (DescriptorTable)
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRange;
  rootParameters[0].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange);

  // パラメータ1: PerFrame (CBV)
  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[1].Descriptor.ShaderRegister = 0; // b0

  // パラメータ2: フリーリストインデックス (RootUAV)
  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[2].Descriptor.ShaderRegister = 1; // u1

  // パラメータ3: フリーリスト (RootUAV)
  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[3].Descriptor.ShaderRegister = 2; // u2

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);

  Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
  hr = D3D12SerializeRootSignature(&descriptionRootSignature,
                                   D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob,
                                   &errorBlob);
  if (FAILED(hr)) {
    Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    assert(false);
  }
  hr = dx12Core_->GetDevice()->CreateRootSignature(
      0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
      IID_PPV_ARGS(&updateComputeRootSignature_));
  assert(SUCCEEDED(hr));

  // Compute Shader のコンパイル
  Microsoft::WRL::ComPtr<IDxcBlob> computeShaderBlob = nullptr;
  computeShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/UpdateParticle.CS.hlsl", L"cs_6_0");
  assert(computeShaderBlob != nullptr);

  // PSO の作成
  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = updateComputeRootSignature_.Get();
  psoDesc.CS = {computeShaderBlob->GetBufferPointer(),
                computeShaderBlob->GetBufferSize()};

  hr = dx12Core_->GetDevice()->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&updateComputePipelineState_));
  assert(SUCCEEDED(hr));
}

void ParticleManager::Emit() {
  auto commandList = dx12Core_->GetCommandList();

  // 全てのエミッターに対して処理を行う
  for (auto emitter : emitters_) {
    if (!emitter) continue;

    if (emitter->needsClear_) {
      InitializeEmitter(emitter, false);
      emitter->needsClear_ = false;
    }

    // FreeListIndexResource と FreeListResource は基本的にUAVのまま運用されるが、念のため明示的に遷移させる
    dx12Core_->TransitionResource(emitter->GetFreeListIndexResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dx12Core_->TransitionResource(emitter->GetFreeListResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  // 実際には、gpuParticleResource だけ SRV -> UAV に遷移させます。
  for (auto emitter : emitters_) {
      if (!emitter) continue;
      dx12Core_->TransitionResource(emitter->GetGPUParticleResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  }

  // --- 1. パーティクルの更新（Update） ---
  commandList->SetComputeRootSignature(updateComputeRootSignature_.Get());
  commandList->SetPipelineState(updateComputePipelineState_.Get());

  for (auto emitter : emitters_) {
      if (!emitter) continue;
      commandList->SetComputeRootDescriptorTable(
          0, srvManager_->GetGPUDescriptorHandle(emitter->GetGPUParticleUavIndex()));
      commandList->SetComputeRootConstantBufferView(
          1, perFrameResource_->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(
          2, emitter->GetFreeListIndexResource()->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(
          3, emitter->GetFreeListResource()->GetGPUVirtualAddress());
      
      // kMaxParticles(1024) に合わせたディスパッチ
      commandList->Dispatch((1024 + 1023) / 1024, 1, 1);
  }

  // --- UAVバリア（書き込み完了待ち） ---
  // Updateが終わるのを待つ
  for (auto emitter : emitters_) {
      if (!emitter) continue;
      D3D12_RESOURCE_BARRIER barrierUAV_Sync[3]{};
      barrierUAV_Sync[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrierUAV_Sync[0].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrierUAV_Sync[0].UAV.pResource = emitter->GetGPUParticleResource();

      barrierUAV_Sync[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrierUAV_Sync[1].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrierUAV_Sync[1].UAV.pResource = emitter->GetFreeListIndexResource();

      barrierUAV_Sync[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrierUAV_Sync[2].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrierUAV_Sync[2].UAV.pResource = emitter->GetFreeListResource();

      commandList->ResourceBarrier(3, barrierUAV_Sync);
  }

  // --- 2. パーティクルの発生（Emit） ---
  commandList->SetComputeRootSignature(emitComputeRootSignature_.Get());
  commandList->SetPipelineState(emitComputePipelineState_.Get());

  for (auto emitter : emitters_) {
    if (emitter && emitter->IsEmitting()) {
      commandList->SetComputeRootDescriptorTable(
          0, srvManager_->GetGPUDescriptorHandle(emitter->GetGPUParticleUavIndex()));
      commandList->SetComputeRootConstantBufferView(
          1, emitter->GetEmitterResource()->GetGPUVirtualAddress());
      commandList->SetComputeRootConstantBufferView(
          2, perFrameResource_->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(
          3, emitter->GetFreeListIndexResource()->GetGPUVirtualAddress());
      commandList->SetComputeRootUnorderedAccessView(
          4, emitter->GetFreeListResource()->GetGPUVirtualAddress());

      commandList->Dispatch(1, 1, 1);
      
      // 発射後にフラグをリセットして毎フレーム出続けるのを防ぐ
      emitter->SetEmit(false);
    }
  }

  // リソースバリア: 描画読み取り用（SRV）に状態遷移
  for (auto emitter : emitters_) {
      if (!emitter) continue;
      dx12Core_->TransitionResource(emitter->GetGPUParticleResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
}
