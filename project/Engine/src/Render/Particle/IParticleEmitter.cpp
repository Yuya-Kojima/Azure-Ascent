#include "Particle/IParticleEmitter.h"
#include "Particle/ParticleManager.h"
#include "Texture/TextureManager.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Math/MathUtil.h"

IParticleEmitter::~IParticleEmitter() {
    ParticleManager::GetInstance()->UnregisterEmitter(this);
}

void IParticleEmitter::BaseInitialize(const std::string& textureFilePath) {
    textureFilePath_ = textureFilePath;
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
    textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);

    randomEngine_ = std::mt19937(seedGenerator_());

    CreateInstancingResource();
    CreateMaterialResource();
}

void IParticleEmitter::CreateInstancingResource() {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();
    auto srvManager = ParticleManager::GetInstance()->GetSrvManager();

    gpuParticleResource_ = dx12Core->CreateUAVBufferResource(sizeof(ParticleCS) * kNumMaxInstance_);
    freeListIndexResource_ = dx12Core->CreateUAVBufferResource(sizeof(int32_t));
    freeListResource_ = dx12Core->CreateUAVBufferResource(sizeof(uint32_t) * kNumMaxInstance_);

    gpuParticleSrvIndex_ = srvManager->Allocate();
    srvManager->CreateSRVforStructuredBuffer(
        gpuParticleSrvIndex_, gpuParticleResource_.Get(),
        kNumMaxInstance_, sizeof(ParticleCS));

    gpuParticleUavIndex_ = srvManager->Allocate();
    srvManager->CreateUAVforStructuredBuffer(
        gpuParticleUavIndex_, gpuParticleResource_.Get(),
        kNumMaxInstance_, sizeof(ParticleCS));

    emitterResource_ = dx12Core->CreateBufferResource(sizeof(EmitterData));
    emitterResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitterData_));
    
    // デフォルト値の初期化
    emitterData_->translate = { 0.0f, 0.0f, 0.0f };
    emitterData_->spawnArea = { 1.0f, 1.0f, 1.0f };
    emitterData_->count = 3;
    emitterData_->frequency = 0.5f;
    emitterData_->frequencyTime = 0.0f;
    emitterData_->emit = 0;
    emitterData_->baseColor = { 1.0f, 1.0f, 1.0f };
    emitterData_->lifeMin = 1.0f;
    emitterData_->lifeMax = 3.0f;
    emitterData_->baseVelocity = { 0.0f, 0.0f, 0.0f };
    emitterData_->velocityRandom = { 0.1f, 0.1f, 0.1f };
    emitterData_->baseScale = { 0.1f, 0.1f, 0.1f };
    emitterData_->scaleRandom = { 0.4f, 0.4f, 0.4f };
    emitterData_->baseRotate = { 0.0f, 0.0f, 0.0f };
    emitterData_->rotateRandom = { 0.0f, 0.0f, 0.0f };
    emitterData_->scaleVelocity = { 0.0f, 0.0f, 0.0f };
    emitterData_->isConverge = 0.0f;

    ParticleManager::GetInstance()->RegisterEmitter(this);
    ParticleManager::GetInstance()->InitializeEmitter(this);
}

void IParticleEmitter::CreateMaterialResource() {
    auto dx12Core = ParticleManager::GetInstance()->GetDx12Core();

    materialResource_ = dx12Core->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));

    materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData_->enableLighting = 0;
    materialData_->isBillboard = 1; // デフォルトはビルボード
    materialData_->isRingMode = 0;
    materialData_->uvTransform = MakeIdentity4x4();
}

void IParticleEmitter::ClearParticles() {
    needsClear_ = true;
}

void IParticleEmitter::Emit(const ParticleEmitDesc& desc) {
    if (!emitterData_) return;

    emitterData_->translate = desc.position;
    emitterData_->spawnArea = desc.spawnRandom; 

    emitterData_->count = desc.count;
    // frequency は Compute Shader 側ではなく、CPU側の ParticleEmitter で管理するためここでは不要
    // Compute Shader 側では emit フラグを見て射出する
    emitterData_->emit = 1;

    emitterData_->baseColor = { desc.color.x, desc.color.y, desc.color.z };
    emitterData_->lifeMin = desc.lifeMin;
    emitterData_->lifeMax = desc.lifeMax;

    emitterData_->baseVelocity = desc.baseVelocity;
    emitterData_->velocityRandom = desc.velocityRandom;

    emitterData_->baseScale = desc.baseScale;
    emitterData_->scaleRandom = desc.scaleRandom;

    emitterData_->baseRotate = desc.baseRotate;
    emitterData_->rotateRandom = desc.rotateRandom;
    emitterData_->scaleVelocity = desc.scaleVelocity;
    emitterData_->isConverge = desc.isConverge ? 1.0f : 0.0f;
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
    commandList->SetPipelineState(blendMode_ == BlendMode::Alpha
        ? ParticleManager::GetInstance()->GetAlphaPipelineState()
        : ParticleManager::GetInstance()->GetPipelineState());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

    // 0: Material CBV
    commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

    // 1: GPU Particle SRV (自身が持つSRVを使用)
    D3D12_GPU_DESCRIPTOR_HANDLE particleSrvHandleGPU =
        srvManager->GetGPUDescriptorHandle(gpuParticleSrvIndex_);
    commandList->SetGraphicsRootDescriptorTable(1, particleSrvHandleGPU);

    // 2: Texture SRV
    D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU =
        srvManager->GetGPUDescriptorHandle(textureSrvIndex_);
    commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

    // 3: PerView CBV
    commandList->SetGraphicsRootConstantBufferView(3, ParticleManager::GetInstance()->GetPerViewResource()->GetGPUVirtualAddress());

    // 常に最大数(kNumMaxInstance_)を描画する（GPUのFreeList方式のため）
    commandList->DrawInstanced(vertexCount_, kNumMaxInstance_, 0, 0);
}
