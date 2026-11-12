#include "Render/Light/TiledLightCuller.h"
#include "Camera/ICamera.h"
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Core/WindowSystem.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include <cassert>

void TiledLightCuller::Initialize(Dx12Core *dx12Core, SrvManager *srvManager, LightManager *lightManager) {
	dx12Core_ = dx12Core;
	srvManager_ = srvManager;
	lightManager_ = lightManager;

	// 3D はいつも画面と同じ大きさの絵に描くので、マスもその大きさで切る（端のはみ出しは切り上げ）
	screenWidth_ = WindowSystem::kClientWidth;
	screenHeight_ = WindowSystem::kClientHeight;
	tileCountX_ = (screenWidth_ + kTileSize_ - 1) / kTileSize_;
	tileCountY_ = (screenHeight_ + kTileSize_ - 1) / kTileSize_;

	viewport_.Width = float(screenWidth_);
	viewport_.Height = float(screenHeight_);
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;
	scissorRect_.right = LONG(screenWidth_);
	scissorRect_.bottom = LONG(screenHeight_);

	CreateDepthTexture();
	CreateTileLightsBuffer();
	CreateComputePipeline();
}

void TiledLightCuller::CreateDepthTexture() {
	ID3D12Device *device = dx12Core_->GetDevice();

	// DSV（D32_FLOAT）と SRV（R32_FLOAT）の両方で見るので、型を決めない R32 で作る
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = screenWidth_;
	resourceDesc.Height = screenHeight_;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	// 1.0（一番遠い）でクリアする
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;

	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthResource_));
	assert(SUCCEEDED(hr));
	depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	// 自分用の DSV ヒープ（1 つ）
	dsvHeap_ = dx12Core_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	device->CreateDepthStencilView(depthResource_.Get(), &dsvDesc,
	                               dsvHeap_->GetCPUDescriptorHandleForHeapStart());

	// CS が読む SRV
	depthSrv_ = srvManager_->Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(depthResource_.Get(), &srvDesc,
	                                 srvManager_->GetCPUDescriptorHandle(depthSrv_.Index()));
}

void TiledLightCuller::CreateTileLightsBuffer() {
	// マスの数 × 1 マスの長さ の uint の並び（1280×720 で 3600 × 322 × 4 バイト ≒ 4.4 MB）
	const size_t sizeInBytes = size_t(tileCountX_) * tileCountY_ * kTileStride_ * sizeof(uint32_t);
	tileLightsResource_ = dx12Core_->CreateUAVBufferResource(sizeInBytes);
}

void TiledLightCuller::CreateComputePipeline() {
	ID3D12Device *device = dx12Core_->GetDevice();

	D3D12_ROOT_PARAMETER rootParameters[kRootParameterCount_] = {};

	rootParameters[kRootParams_].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[kRootParams_].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootParams_].Descriptor.ShaderRegister = 0;

	rootParameters[kRootPointLights_].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[kRootPointLights_].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootPointLights_].Descriptor.ShaderRegister = 0;

	rootParameters[kRootSpotLights_].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
	rootParameters[kRootSpotLights_].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootSpotLights_].Descriptor.ShaderRegister = 1;

	// 深度だけの絵はテクスチャなので、ルート SRV ではなく表で渡す
	D3D12_DESCRIPTOR_RANGE depthRange[1] = {};
	depthRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	depthRange[0].NumDescriptors = 1;
	depthRange[0].BaseShaderRegister = 2;
	depthRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	rootParameters[kRootDepth_].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[kRootDepth_].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootDepth_].DescriptorTable.pDescriptorRanges = depthRange;
	rootParameters[kRootDepth_].DescriptorTable.NumDescriptorRanges = _countof(depthRange);

	rootParameters[kRootTileLights_].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
	rootParameters[kRootTileLights_].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	rootParameters[kRootTileLights_].Descriptor.ShaderRegister = 0;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                         signatureBlob.GetAddressOf(), errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		if (errorBlob) {
			Logger::Log(reinterpret_cast<char *>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
	                                 IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));

	IDxcBlob *computeShaderBlob = dx12Core_->CompileShader(L"resources/shaders/LightCulling.CS.hlsl", L"cs_6_0");
	assert(computeShaderBlob != nullptr);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature_.Get();
	pipelineDesc.CS = {computeShaderBlob->GetBufferPointer(), computeShaderBlob->GetBufferSize()};
	hr = device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&pipelineState_));
	assert(SUCCEEDED(hr));
	computeShaderBlob->Release();

	// 定数バッファは 256 バイト単位で作る
	const size_t paramsSize = (sizeof(CullingParams) + 255) & ~size_t(255);
	paramsResource_ = dx12Core_->CreateBufferResource(paramsSize);
	paramsResource_->Map(0, nullptr, reinterpret_cast<void **>(&paramsData_));
	*paramsData_ = {};
}

void TiledLightCuller::TransitionDepth(ID3D12GraphicsCommandList *commandList, D3D12_RESOURCE_STATES newState) {
	if (depthState_ == newState) {
		return;
	}
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = depthResource_.Get();
	barrier.Transition.StateBefore = depthState_;
	barrier.Transition.StateAfter = newState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &barrier);
	depthState_ = newState;
}

void TiledLightCuller::BeginPrepass() {
	// オン・オフはフレームの途中で変わらないよう、ここで決めた値をこのフレームの間使う
	isActive_ = enabled_;
	isRecordingPrepass_ = false;
	if (!isActive_) {
		return;
	}
	++prepassFrame_;

	ID3D12GraphicsCommandList *commandList = dx12Core_->GetPrepassCommandList();

	// P は A・B と別の列なので、描画の設定を一から積む
	srvManager_->PreDraw(commandList);
	commandList->RSSetViewports(1, &viewport_);
	commandList->RSSetScissorRects(1, &scissorRect_);

	// 深度だけの絵を描画先にして（色の描画先はなし）、一番遠い 1.0 でクリアする
	TransitionDepth(commandList, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
	commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	isRecordingPrepass_ = true;
}

void TiledLightCuller::Dispatch(const ICamera *camera) {
	if (!isActive_) {
		return;
	}
	isRecordingPrepass_ = false;

	// カメラがないとマスの形が決まらない。名簿は前の内容のまま残る
	if (!camera) {
		return;
	}

	// 定数: カメラの行列と、ライトの数・マスの数・画面の大きさ
	const Matrix4x4 projectionInverse = Inverse(camera->GetProjectionMatrix());
	paramsData_->view = camera->GetViewMatrix();
	paramsData_->projectionInverse = projectionInverse;
	paramsData_->nearZ = TransformPoint(Vector3{0.0f, 0.0f, 0.0f}, projectionInverse).z;
	paramsData_->tileCountX = tileCountX_;
	paramsData_->tileCountY = tileCountY_;
	paramsData_->pointLightCount = lightManager_->GetPointLightCount();
	paramsData_->spotLightCount = lightManager_->GetSpotLightCount();
	paramsData_->screenWidth = float(screenWidth_);
	paramsData_->screenHeight = float(screenHeight_);

	ID3D12GraphicsCommandList *commandList = dx12Core_->GetPrepassCommandList();

	// 深度だけの絵を CS から読める状態に、名簿を CS から書ける状態にする。
	// 名簿はバッファなので、前のフレームの ExecuteCommandLists が終わった時点で COMMON に戻っている
	TransitionDepth(commandList, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	D3D12_RESOURCE_BARRIER toUav{};
	toUav.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toUav.Transition.pResource = tileLightsResource_.Get();
	toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	toUav.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	commandList->ResourceBarrier(1, &toUav);

	commandList->SetComputeRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->SetComputeRootConstantBufferView(kRootParams_, paramsResource_->GetGPUVirtualAddress());
	commandList->SetComputeRootShaderResourceView(kRootPointLights_, lightManager_->GetPointLightsAddress());
	commandList->SetComputeRootShaderResourceView(kRootSpotLights_, lightManager_->GetSpotLightsAddress());
	commandList->SetComputeRootDescriptorTable(kRootDepth_, srvManager_->GetGPUDescriptorHandle(depthSrv_.Index()));
	commandList->SetComputeRootUnorderedAccessView(kRootTileLights_, tileLightsResource_->GetGPUVirtualAddress());

	// 1 グループが 1 マス
	commandList->Dispatch(tileCountX_, tileCountY_, 1);

	// B のピクセルシェーダーが読める状態にする
	D3D12_RESOURCE_BARRIER toSrv = toUav;
	toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	commandList->ResourceBarrier(1, &toSrv);
}
