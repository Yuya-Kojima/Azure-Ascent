#include "Particle/IParticleEmitter.h"
#include "Particle/ParticleManager.h"
#include "Texture/TextureManager.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include <algorithm>

IParticleEmitter::~IParticleEmitter() {
    ParticleManager::GetInstance()->UnregisterEmitter(this);
}

void IParticleEmitter::BaseInitialize(const std::string& textureFilePath, uint32_t maxParticles) {
    textureFilePath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);

    randomEngine_ = std::mt19937(seedGenerator_());
    maxParticles_ = (std::max)(maxParticles, 1u);

    CreateInstancingResource();
    CreateMaterialResource();
}

void IParticleEmitter::CreateInstancingResource() {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();
    auto srvManager = ParticleManager::GetInstance()->GetSrvManager();

    // 粒の本体と空きの列は、上限の数だけ用意する
    gpuParticleResource_ = dx12Core->CreateUAVBufferResource(sizeof(ParticleCS) * maxParticles_);
    freeListIndexResource_ = dx12Core->CreateUAVBufferResource(sizeof(int32_t));
    freeListResource_ = dx12Core->CreateUAVBufferResource(sizeof(uint32_t) * maxParticles_);

    gpuParticleSrv_ = srvManager->Allocate();
    srvManager->CreateSRVforStructuredBuffer(
        gpuParticleSrv_.Index(), gpuParticleResource_.Get(),
        maxParticles_, sizeof(ParticleCS));

    gpuParticleUav_ = srvManager->Allocate();
    srvManager->CreateUAVforStructuredBuffer(
        gpuParticleUav_.Index(), gpuParticleResource_.Get(),
        maxParticles_, sizeof(ParticleCS));

    // 出す頼みを GPU へ渡すバッファ（CPU から毎フレーム書く）
    emitRequestResource_ = dx12Core->CreateBufferResource(sizeof(EmitterData) * kMaxEmitRequests);
    emitRequestResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitRequestData_));
    emitRequests_.reserve(kMaxEmitRequests);

    // 力の初期値は 0（まっすぐ同じ速さで進む）
    motionResource_ = dx12Core->CreateBufferResource(sizeof(ParticleMotion));
    motionResource_->Map(0, nullptr, reinterpret_cast<void**>(&motionData_));
    motionData_->gravity = { 0.0f, 0.0f, 0.0f };
    motionData_->drag = 0.0f;
    motionData_->wind = { 0.0f, 0.0f, 0.0f };
    motionData_->padding0 = 0.0f;

    ParticleManager::GetInstance()->RegisterEmitter(this);
    ParticleManager::GetInstance()->InitializeEmitter(this);
}

void IParticleEmitter::CreateMaterialResource() {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();

    materialResource_ = dx12Core->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 0;
    materialData_->isBillboard = kBillboardFlat_; // デフォルトはビルボード
    materialData_->isRingMode = 0;
    materialData_->isFireMode = static_cast<int32_t>(FireMode::Off);
    materialData_->uvTransform = MakeIdentity4x4();
    materialData_->fireNoiseOrigin = { 0.0f, 0.0f, 0.0f };
    materialData_->fireTime = 0.0f;
    materialData_->streakSeconds = 0.0f;
    materialData_->streakWidthRate = 1.0f;
    materialData_->padding[0] = 0.0f;
    materialData_->padding[1] = 0.0f;
}

void IParticleEmitter::ClearParticles() {
    needsClear_ = true;
    // まだ GPU に渡していない出す頼みも取り消す
    emitRequests_.clear();
}

void IParticleEmitter::Emit(const ParticleEmitDesc& desc) {
    if (desc.count == 0) return;
    // 列がいっぱいのフレームは、それ以上の頼みを捨てる
    if (emitRequests_.size() >= kMaxEmitRequests) {
        Logger::Log("[IParticleEmitter] 1 フレームの出す頼みが上限を超えたので捨てた\n");
        return;
    }

    EmitterData request{};
    request.translate = desc.position;
    request.spawnArea = desc.spawnRandom;
    request.count = desc.count;

    request.baseColor = { desc.color.x, desc.color.y, desc.color.z };
    request.lifeMin = desc.lifeMin;
    request.lifeMax = desc.lifeMax;

    request.baseVelocity = desc.baseVelocity;
    request.velocityRandom = desc.velocityRandom;

    request.baseScale = desc.baseScale;
    request.scaleRandom = desc.scaleRandom;

    request.baseRotate = desc.baseRotate;
    request.rotateRandom = desc.rotateRandom;
    request.scaleVelocity = desc.scaleVelocity;
    request.isConverge = desc.isConverge ? 1.0f : 0.0f;

    request.weightMin = desc.weightMin;
    request.weightMax = desc.weightMax;
    request.colorEnd = desc.colorEnd.value_or(request.baseColor);
    request.scaleEndRate = desc.scaleEndRate;
    request.fadeStart = desc.fadeStart;

    request.emitShape = static_cast<uint32_t>(desc.shape);
    request.boxWorld = desc.boxWorld;
    request.boxHalfSize = desc.boxHalfSize;
    request.bandCenter = desc.bandCenter;
    request.bandInner = desc.bandInner;
    request.bandOuter = desc.bandOuter;
    request.surfaceSpeed = desc.surfaceSpeed;
    request.rotateVelocityRandom = desc.rotateVelocityRandom;

    emitRequests_.push_back(request);
}

uint32_t IParticleEmitter::FlushEmitRequests(uint32_t& maxCount) {
    maxCount = 0;
    const uint32_t requestCount = static_cast<uint32_t>(emitRequests_.size());
    for (uint32_t i = 0; i < requestCount; ++i) {
        emitRequestData_[i] = emitRequests_[i];
        maxCount = (std::max)(maxCount, emitRequests_[i].count);
    }
    emitRequests_.clear();
    return requestCount;
}

void IParticleEmitter::Emit(const Vector3& position, uint32_t count,
    const Vector3& baseVelocity, const Vector3& velocityRandom,
    const Vector3& spawnRandom, float lifeMin, float lifeMax,
    const Vector4& color, const Vector3& baseScale, const Vector3& scaleRandom,
    const Vector3& baseRotate, const Vector3& rotateRandom, const Vector3& scaleVelocity) {
    
    ParticleEmitDesc desc{};
    desc.position = position;
    desc.count = count;
    desc.baseVelocity = baseVelocity;
    desc.velocityRandom = velocityRandom;
    desc.spawnRandom = spawnRandom;
    desc.lifeMin = lifeMin;
    desc.lifeMax = lifeMax;
    desc.color = color;
    desc.baseScale = baseScale;
    desc.scaleRandom = scaleRandom;
    desc.baseRotate = baseRotate;
    desc.rotateRandom = rotateRandom;
    desc.scaleVelocity = scaleVelocity;

    Emit(desc);
}

void IParticleEmitter::Draw() {
    if (vertexCount_ == 0) return;

    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();
    auto srvManager = ParticleManager::GetInstance()->GetSrvManager();
    auto commandList = dx12Core->GetCommandList();

    commandList->SetGraphicsRootSignature(ParticleManager::GetInstance()->GetRootSignature());
    ID3D12PipelineState* pipelineState = ParticleManager::GetInstance()->GetPipelineState();
    if (blendMode_ == BlendMode::Alpha) {
        pipelineState = ParticleManager::GetInstance()->GetAlphaPipelineState();
    } else if (blendMode_ == BlendMode::Premultiplied) {
        pipelineState = ParticleManager::GetInstance()->GetPremultipliedPipelineState();
    }
    commandList->SetPipelineState(pipelineState);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // 0: Material CBV
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    // 1: GPU Particle SRV (自身が持つSRVを使用)
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvHandleGPU =
        srvManager->GetGPUDescriptorHandle(gpuParticleSrv_.Index());
    commandList->SetGraphicsRootDescriptorTable(1, particleSrvHandleGPU);

    // 2: Texture SRV
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU =
        srvManager->GetGPUDescriptorHandle(textureSrvIndex_);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

    // 3: PerView CBV
    commandList->SetGraphicsRootConstantBufferView(3, ParticleManager::GetInstance()->GetPerViewResource()->GetGPUVirtualAddress());

    // 常に最大数(maxParticles_)を描画する（GPUのFreeList方式のため）
    commandList->DrawInstanced(vertexCount_, maxParticles_, 0, 0);
}
