#include "Renderer/Object3dRenderer.h"
#include "Debug/Logger.h"
#include "Util/StringUtil.h"

void Object3dRenderer::Initialize(Dx12Core *dx12Core) {
  dx12Core_ = dx12Core;

  CreatePSO();
  CreateSkinningComputePSO();

  CreateDirectionalLightData();

  CreatePointLightData();

  CreateSpotLightData();

  CreateFogData();
}

void Object3dRenderer::CreateRootSignature() {

  HRESULT hr;

  ID3D12Device *device = dx12Core_->GetDevice();

  // RootSignatureを作成
  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  // Object3d用のRootParameterを作成
  D3D12_ROOT_PARAMETER rootParameterObject3d[10] = {};
  rootParameterObject3d[0].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[0].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
  rootParameterObject3d[0].Descriptor.ShaderRegister =
      0; // レジスタ番号0とバインド

  rootParameterObject3d[1].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[1].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_VERTEX;                     // VertexShaderで使う
  rootParameterObject3d[1].Descriptor.ShaderRegister = 0; // レジスタ番号0を使う

  D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
  descriptorRange[0].BaseShaderRegister = 0; // ０から始まる
  descriptorRange[0].NumDescriptors = 1;     // 数は1つ
  descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使う
  descriptorRange[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動計算

  rootParameterObject3d[2].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使う
  rootParameterObject3d[2].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使う
  rootParameterObject3d[2].DescriptorTable.pDescriptorRanges =
      descriptorRange; // Tableの中身の配列を指定
  rootParameterObject3d[2].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRange); // Tableで利用する数

  rootParameterObject3d[3].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[3].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;                      // PSで使う
  rootParameterObject3d[3].Descriptor.ShaderRegister = 1; // レジスタ番号1を使う

  rootParameterObject3d[4].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[4].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;                      // PSで使う
  rootParameterObject3d[4].Descriptor.ShaderRegister = 2; // レジスタ番号2を使う

  rootParameterObject3d[5].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[5].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;                      // PSで使う
  rootParameterObject3d[5].Descriptor.ShaderRegister = 3; // レジスタ番号3を使う

  rootParameterObject3d[6].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV; // CBVを使う
  rootParameterObject3d[6].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;                      // PSで使う
  rootParameterObject3d[6].Descriptor.ShaderRegister = 4; // レジスタ番号4を使う

  D3D12_DESCRIPTOR_RANGE descriptorRangeEnvMap[1] = {};
  descriptorRangeEnvMap[0].BaseShaderRegister = 1; // t1
  descriptorRangeEnvMap[0].NumDescriptors = 1;
  descriptorRangeEnvMap[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRangeEnvMap[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameterObject3d[7].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameterObject3d[7].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameterObject3d[7].DescriptorTable.pDescriptorRanges =
      descriptorRangeEnvMap;
  rootParameterObject3d[7].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRangeEnvMap);

  D3D12_DESCRIPTOR_RANGE descriptorRangeMaskMap[1] = {};
  descriptorRangeMaskMap[0].BaseShaderRegister = 2; // t2
  descriptorRangeMaskMap[0].NumDescriptors = 1;
  descriptorRangeMaskMap[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRangeMaskMap[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameterObject3d[8].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameterObject3d[8].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameterObject3d[8].DescriptorTable.pDescriptorRanges =
      descriptorRangeMaskMap;
  rootParameterObject3d[8].DescriptorTable.NumDescriptorRanges =
      _countof(descriptorRangeMaskMap);

  // フォグ用データ (b5)
  rootParameterObject3d[9].ParameterType =
      D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameterObject3d[9].ShaderVisibility =
      D3D12_SHADER_VISIBILITY_PIXEL;
  rootParameterObject3d[9].Descriptor.ShaderRegister = 5;

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

  descriptionRootSignature.pParameters =
      rootParameterObject3d; // ルートパラメータ配列へのポインタ
  descriptionRootSignature.NumParameters =
      _countof(rootParameterObject3d); // 配列の長さ
  descriptionRootSignature.pStaticSamplers = staticSamplers;
  descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

  // シリアライズしてバイナリにする
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

  // バイナリを元に生成
  rootSignature_ = nullptr;
  hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                   signatureBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&rootSignature_));
  assert(SUCCEEDED(hr));
}

void Object3dRenderer::CreatePSO() {
  HRESULT hr;

  ID3D12Device *device = dx12Core_->GetDevice();

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

  blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

  blendDesc.RenderTarget[0].SrcBlendAlpha =
      D3D12_BLEND_ONE; // 元画像の透明度をブレンドに反映

  blendDesc.RenderTarget[0].BlendOpAlpha =
      D3D12_BLEND_OP_ADD; // 上二つを合成(加算)

  blendDesc.RenderTarget[0].DestBlendAlpha =
      D3D12_BLEND_INV_SRC_ALPHA; // 背景の透明度は無視

  // RasterizerStateの設定
  D3D12_RASTERIZER_DESC rasterizerDesc{};

  // 裏面(時計回り)を表示しない
  rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
  rasterizerDesc.FrontCounterClockwise = FALSE;

  // 三角形の中を塗りつぶす
  rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

  // shaderをcompileする
  IDxcBlob *vertexShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Object3d.VS.hlsl", L"vs_6_0");
  assert(vertexShaderBlob != nullptr);

  IDxcBlob *geometryShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Object3d.GS.hlsl", L"gs_6_0");
  assert(geometryShaderBlob != nullptr);

  IDxcBlob *pixelShaderBlob = dx12Core_->CompileShader(
      L"resources/shaders/Object3d.PS.hlsl", L"ps_6_0");
  assert(pixelShaderBlob != nullptr);

  // DepthStencilStateの設定
  D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

  // Depthの機能を有効化する
  depthStencilDesc.DepthEnable = true;

  // 書き込み
  depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

  // 比較関数はLessEqual。近ければ描画される
  depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

  // PSOの生成
  D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipeLineStateDesc{};
  graphicsPipeLineStateDesc.pRootSignature =
      rootSignature_.Get();                                // RootSignature
  graphicsPipeLineStateDesc.InputLayout = inputLayoutDesc; // InputLayout
  graphicsPipeLineStateDesc.VS = {
      vertexShaderBlob->GetBufferPointer(),
      vertexShaderBlob->GetBufferSize()}; // VertexShader
  graphicsPipeLineStateDesc.GS = {
      geometryShaderBlob->GetBufferPointer(),
      geometryShaderBlob->GetBufferSize()}; // geometryShader
  graphicsPipeLineStateDesc.PS = {
      pixelShaderBlob->GetBufferPointer(),
      pixelShaderBlob->GetBufferSize()};                      // PixelShader
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
  hr = device->CreateGraphicsPipelineState(
      &graphicsPipeLineStateDesc, IID_PPV_ARGS(&graphicsPipeLineState_));
  assert(SUCCEEDED(hr));

  // Depthを無効にしたPSOを生成
  depthStencilDesc.DepthEnable = false;
  graphicsPipeLineStateDesc.DepthStencilState = depthStencilDesc;
  
  graphicsPipeLineStateDepthDisabled_ = nullptr;
  hr = device->CreateGraphicsPipelineState(
      &graphicsPipeLineStateDesc, IID_PPV_ARGS(&graphicsPipeLineStateDepthDisabled_));
  assert(SUCCEEDED(hr));
}

void Object3dRenderer::Begin() {
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList =
      dx12Core_->GetCommandList();

  // RootSignatureをセット
  commandList->SetGraphicsRootSignature(rootSignature_.Get());

  // PSOをセット
  commandList->SetPipelineState(graphicsPipeLineState_.Get());

  // プリミティブトポロジー(形状）をセット
  commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Object3dRenderer::SetDepthEnable(bool enable) {
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList =
      dx12Core_->GetCommandList();
  
  if (enable) {
      commandList->SetPipelineState(graphicsPipeLineState_.Get());
  } else {
      commandList->SetPipelineState(graphicsPipeLineStateDepthDisabled_.Get());
  }
}

void Object3dRenderer::CreateDirectionalLightData() {
  UINT size = (sizeof(DirectionalLight) + 255) & ~255;
  directionalLightResource_ = dx12Core_->CreateBufferResource(size);

  // データを書き込む
  directionalLightData_ = nullptr;

  // 書き込むためのアドレスを取得
  directionalLightResource_->Map(
      0, nullptr, reinterpret_cast<void **>(&directionalLightData_));

  // Lightingの色
  directionalLightData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
  directionalLightData_->direction = Normalize(Vector3(0.0f, -1.0f, 0.0f));
  directionalLightData_->intensity = 1.0f;
  directionalLightData_->skyAmbient = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
  directionalLightData_->groundAmbient = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
  directionalLightData_->backRimColor = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
  directionalLightData_->backRimPower = 2.5f;
  directionalLightData_->backRimDirection = Vector3(0.0f, 0.0f, -1.0f);
  directionalLightData_->debugView = 0;
}

void Object3dRenderer::CreatePointLightData() {

  UINT pointLightSize = (sizeof(PointLight) + 255) & ~255;
  pointLightResource_ = dx12Core_->CreateBufferResource(pointLightSize);

  // データを書き込む
  pointLightData_ = nullptr;

  // 書き込むためのアドレスを取得
  pointLightResource_->Map(0, nullptr,
                           reinterpret_cast<void **>(&pointLightData_));

  pointLightData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
  pointLightData_->position = {0.0f, 2.0f, 0.0f};
  pointLightData_->intensity = 0.0f;
  pointLightData_->radius = 5.0f;
  pointLightData_->decay = 2.0f;
}

void Object3dRenderer::CreateSpotLightData() {
  UINT spotLightSize = (sizeof(SpotLight) + 255) & ~255;
  spotLightResource_ = dx12Core_->CreateBufferResource(spotLightSize);

  // データを書き込む
  spotLightData_ = nullptr;

  // 書き込むためのアドレスを取得
  spotLightResource_->Map(0, nullptr,
                          reinterpret_cast<void **>(&spotLightData_));

  spotLightData_->color = {1.0f, 1.0f, 1.0f, 1.0f};
  spotLightData_->position = {2.0f, 1.25f, 0.0f};
  spotLightData_->intensity = 0.0f;
  spotLightData_->distance = 7.0f;
  spotLightData_->direction = Normalize({-1.0f, -1.0f, 0.0f});
  spotLightData_->decay = 2.0f;
  spotLightData_->cosAngle = std::cos(std::numbers::pi_v<float> / 3.0f);
}



void Object3dRenderer::CreateSkinningComputeRootSignature() {
  HRESULT hr;
  ID3D12Device *device = dx12Core_->GetDevice();

  D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
  descriptionRootSignature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  D3D12_ROOT_PARAMETER rootParameters[5] = {};

  // t0: gMatrixPalette (SRV)
  D3D12_DESCRIPTOR_RANGE descriptorRangePalette[1] = {};
  descriptorRangePalette[0].BaseShaderRegister = 0;
  descriptorRangePalette[0].NumDescriptors = 1;
  descriptorRangePalette[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRangePalette[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[0].DescriptorTable.pDescriptorRanges = descriptorRangePalette;
  rootParameters[0].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangePalette);

  // t1: gInputVertices (SRV)
  D3D12_DESCRIPTOR_RANGE descriptorRangeInputVertices[1] = {};
  descriptorRangeInputVertices[0].BaseShaderRegister = 1;
  descriptorRangeInputVertices[0].NumDescriptors = 1;
  descriptorRangeInputVertices[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRangeInputVertices[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeInputVertices;
  rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInputVertices);

  // t2: gInfluences (SRV)
  D3D12_DESCRIPTOR_RANGE descriptorRangeInfluences[1] = {};
  descriptorRangeInfluences[0].BaseShaderRegister = 2;
  descriptorRangeInfluences[0].NumDescriptors = 1;
  descriptorRangeInfluences[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descriptorRangeInfluences[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRangeInfluences;
  rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeInfluences);

  // u0: gOutputVertices (UAV)
  D3D12_DESCRIPTOR_RANGE descriptorRangeOutputVertices[1] = {};
  descriptorRangeOutputVertices[0].BaseShaderRegister = 0;
  descriptorRangeOutputVertices[0].NumDescriptors = 1;
  descriptorRangeOutputVertices[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  descriptorRangeOutputVertices[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[3].DescriptorTable.pDescriptorRanges = descriptorRangeOutputVertices;
  rootParameters[3].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeOutputVertices);

  // b0: gSkinningInformation (CBV)
  rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootParameters[4].Descriptor.ShaderRegister = 0;

  descriptionRootSignature.pParameters = rootParameters;
  descriptionRootSignature.NumParameters = _countof(rootParameters);

  Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
  Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
  hr = D3D12SerializeRootSignature(&descriptionRootSignature, D3D_ROOT_SIGNATURE_VERSION_1, signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
  if (FAILED(hr)) {
    if (errorBlob) {
      Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
    }
    assert(false);
  }

  skinningComputeRootSignature_ = nullptr;
  hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&skinningComputeRootSignature_));
  assert(SUCCEEDED(hr));
}

void Object3dRenderer::CreateSkinningComputePSO() {
  HRESULT hr;
  ID3D12Device *device = dx12Core_->GetDevice();

  CreateSkinningComputeRootSignature();

  IDxcBlob *computeShaderBlob = dx12Core_->CompileShader(L"resources/shaders/Skinning.CS.hlsl", L"cs_6_0");
  assert(computeShaderBlob != nullptr);

  D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineStateDesc{};
  computePipelineStateDesc.CS = { computeShaderBlob->GetBufferPointer(), computeShaderBlob->GetBufferSize() };
  computePipelineStateDesc.pRootSignature = skinningComputeRootSignature_.Get();

  skinningComputePipelineState_ = nullptr;
  hr = device->CreateComputePipelineState(&computePipelineStateDesc, IID_PPV_ARGS(&skinningComputePipelineState_));
  assert(SUCCEEDED(hr));
}

void Object3dRenderer::CreateFogData() {
  UINT size = (sizeof(FogData) + 255) & ~255;
  fogResource_ = dx12Core_->CreateBufferResource(size);

  fogData_ = nullptr;
  fogResource_->Map(0, nullptr, reinterpret_cast<void **>(&fogData_));

  // デフォルト値
  fogData_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
  fogData_->nearDist = 100.0f;
  fogData_->farDist = 200.0f;
  fogData_->enabled = 0.0f;
}
