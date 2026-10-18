#pragma once
#include "Core/Dx12Core.h"
#include "Math/Matrix4x4.h"
#include "Math/VertexData.h"
#include "Particle/Particle.h"
#include <list>
#include <random>
#include <string>
#include <unordered_map>

class SrvManager;
class IParticleEmitter;

class ParticleManager {

public:

	/// <summary>
	/// シングルトンインスタンスの取得
	/// </summary>
	static ParticleManager* GetInstance();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(Dx12Core* dx12Core, SrvManager* srvManager);

	/// <summary>
	/// 終了
	/// </summary>
	void Finalize();


private:

	ParticleManager() = default;
	~ParticleManager() = default;
	ParticleManager(ParticleManager&) = delete;
	ParticleManager& operator=(ParticleManager&) = delete;


private:
	Dx12Core* dx12Core_ = nullptr;
	SrvManager* srvManager_ = nullptr;

	// ルートシグネチャ (Graphics)
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
	// PSO (Graphics)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipeLineState_ = nullptr;
	// PSO (Graphics、半透明合成)
	Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaPipelineState_ = nullptr;

	// Particleを0クリアするためのComputePipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> initializeComputePipelineState_ = nullptr;

	// ParticleをEmitするためのComputePipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> emitComputeRootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> emitComputePipelineState_ = nullptr;

	// ParticleをUpdateするためのComputePipeline
	Microsoft::WRL::ComPtr<ID3D12RootSignature> updateComputeRootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> updateComputePipelineState_ = nullptr;

	// ComputePipelineの生成
	void CreateUpdateComputePipeline();

	// PerView 用構造体とリソース
	struct PerView {
		Matrix4x4 viewProjection;  // 64 bytes
		Matrix4x4 billboardMatrix; // 64 bytes
		Vector3 cameraPosition;    // 12 bytes
		float padding;             // 4 bytes
		Vector4 padding2[7];       // 112 bytes (合計 256 bytes)
	};
	Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_ = nullptr;
	PerView* perViewData_ = nullptr;

	// PerFrame用構造体とリソース
	struct PerFrame {
		float time;
		float deltaTime;
	};
	Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_ = nullptr;
	PerFrame* perFrameData_ = nullptr;

	const uint32_t kMaxParticles = 1024;

public:
	Dx12Core* GetDx12Core() const { return dx12Core_; }
	SrvManager* GetSrvManager() const { return srvManager_; }
	ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
	ID3D12PipelineState* GetPipelineState() const { return graphicsPipeLineState_.Get(); }
	ID3D12PipelineState* GetAlphaPipelineState() const { return alphaPipelineState_.Get(); }

	ID3D12Resource* GetPerViewResource() const { return perViewResource_.Get(); }
	PerView* GetPerViewData() const { return perViewData_; }

	/// <summary>
	/// エミッターの登録・解除
	/// </summary>
	void RegisterEmitter(IParticleEmitter* emitter) { emitters_.push_back(emitter); }
	void UnregisterEmitter(IParticleEmitter* emitter) { emitters_.remove(emitter); }
	
	/// <summary>
	/// 新しいエミッターのGPUバッファを初期化する
	/// </summary>
	void InitializeEmitter(IParticleEmitter* emitter, bool isFirstInit = true);

	/// <summary>
	/// EmitterなどのCPUでの更新処理
	/// </summary>
	void Update();

	/// <summary>
	/// GPU側でParticleの射出を行う
	/// </summary>
	void Emit();

private:
	/// <summary>
	/// 共有リソースの生成と初期化
	/// </summary>
	void InitializeSharedResources();

	/// <summary>
	/// 初期化CS用のルートシグネチャとPSOを作成
	/// </summary>
	void CreateComputePipeline();

	/// <summary>
	/// 射出CS用のルートシグネチャとPSOを作成
	/// </summary>
	void CreateEmitComputePipeline();

private:
	/// <summary>
	/// ルートシグネチャを作成
	/// </summary>
	void CreateRootSignature();

	/// <summary>
	/// オブジェクト用のPSOを作成
	/// </summary>
	void CreatePSO();

	std::list<IParticleEmitter*> emitters_;
};
