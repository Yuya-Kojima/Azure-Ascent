#pragma once
#include "Core/SrvHandle.h"
#include "Math/Matrix4x4.h"
#include "Render/Light/LightManager.h"
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class Dx12Core;
class SrvManager;
class ICamera;

/// <summary>
/// 画面を 16×16 画素のマスに区切り、マスごとに届くライトの番号の名簿を作る（タイル分け・Forward+）。
/// 1. BeginPrepass: 命令の列 P に、深度だけの絵の準備を積む（このあと Object3d が P に深度だけの描画を積む）
/// 2. Dispatch: P に、深度だけの絵を読んでマスごとの名簿を書く CS を積む
/// Object3d のピクセルシェーダーは、自分のマスの名簿にあるライトだけを足す
/// </summary>
class TiledLightCuller {
public:
	// マスの一辺の画素数（LightCulling.hlsli の kTileSize と同じ値）
	static constexpr uint32_t kTileSize_ = 16;
	// 1 マスの名簿の長さ（uint の数）: [0] 点の数 / [1] スポットの数 / 点の番号 / スポットの番号
	// （LightCulling.hlsli の kTileStride と同じ値）
	static constexpr uint32_t kTileStride_ =
		2 + LightManager::kMaxPointLights_ + LightManager::kMaxSpotLights_;

	void Initialize(Dx12Core *dx12Core, SrvManager *srvManager, LightManager *lightManager);

	/// <summary>
	/// このフレームでマス分けをするかを決め、する場合は P に SRV ヒープ・ビューポート・シザーと、
	/// 深度だけの絵を描画先にしてクリアする命令を積む。Begin3DPass で列を分けた直後に呼ぶ
	/// </summary>
	void BeginPrepass();

	/// <summary>
	/// P に積むのを止め、マス分けの CS を P に積む。シーンの 3D の描画が終わったら呼ぶ
	/// </summary>
	/// <param name="camera">マス分けに使うカメラ（デフォルトカメラ）</param>
	void Dispatch(const ICamera *camera);

	/// <summary>
	/// Object3d を P に積む間か（BeginPrepass 〜 Dispatch の間で、このフレームでマス分けをしているとき）
	/// </summary>
	bool IsRecordingPrepass() const { return isRecordingPrepass_; }

	/// <summary>
	/// このフレームでマス分けをしているか（ピクセルシェーダーがマスの名簿を使うか）
	/// </summary>
	bool IsActive() const { return isActive_; }

	// Tiled Culling のオン・オフ（オフでは P に深度を積まず、CS も動かさない）
	bool IsEnabled() const { return enabled_; }
	void SetEnabled(bool enabled) { enabled_ = enabled; }

	// マスごとのライトの名簿（PS の t5）
	D3D12_GPU_VIRTUAL_ADDRESS GetTileLightsAddress() const { return tileLightsResource_->GetGPUVirtualAddress(); }
	// 横のマスの数
	uint32_t GetTileCountX() const { return tileCountX_; }
	// マス分けをした回の番号（BeginPrepass のたびに増える）
	uint64_t GetPrepassFrame() const { return prepassFrame_; }

private:
	// CS の定数（LightCulling.CS.hlsl の CullingParams と同じ並び）
	struct CullingParams {
		Matrix4x4 view;              // デフォルトカメラの view 行列
		Matrix4x4 projectionInverse; // 射影の逆行列
		float nearZ;                 // 深さ 0 を逆射影したカメラ空間の奥行き
		uint32_t tileCountX;
		uint32_t tileCountY;
		uint32_t pointLightCount;
		uint32_t spotLightCount;
		float screenWidth;
		float screenHeight;
		float padding;
	};

	// ルートシグネチャの番号
	enum RootParameter : uint32_t {
		kRootParams_ = 0,       // 定数（b0）
		kRootPointLights_ = 1,  // 点の名簿（t0）
		kRootSpotLights_ = 2,   // スポットの名簿（t1）
		kRootDepth_ = 3,        // 深度だけの絵（t2、表）
		kRootTileLights_ = 4,   // マスごとの名簿（u0）
		kRootParameterCount_ = 5,
	};

	void CreateDepthTexture();
	void CreateTileLightsBuffer();
	void CreateComputePipeline();

	/// <summary>
	/// 深度だけの絵の状態を移す（P の中で、自分で覚えた状態から）
	/// </summary>
	void TransitionDepth(ID3D12GraphicsCommandList *commandList, D3D12_RESOURCE_STATES newState);

	Dx12Core *dx12Core_ = nullptr;
	SrvManager *srvManager_ = nullptr;
	LightManager *lightManager_ = nullptr;

	uint32_t screenWidth_ = 0;
	uint32_t screenHeight_ = 0;
	uint32_t tileCountX_ = 0;
	uint32_t tileCountY_ = 0;

	// 深度だけの絵（R32_TYPELESS。DSV は D32_FLOAT、SRV は R32_FLOAT）
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
	SrvHandle depthSrv_;
	// 深度だけの絵の今の状態（P は記録する順と動く順が違うので、Dx12Core の記録表を使わずに自分で覚える）
	D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	// マスごとのライトの名簿（デフォルトヒープ・UAV 可）
	Microsoft::WRL::ComPtr<ID3D12Resource> tileLightsResource_;

	// マス分けの CS
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	CullingParams *paramsData_ = nullptr;

	D3D12_VIEWPORT viewport_{};
	D3D12_RECT scissorRect_{};

	bool enabled_ = true;
	bool isActive_ = false;
	bool isRecordingPrepass_ = false;
	uint64_t prepassFrame_ = 0;
};
