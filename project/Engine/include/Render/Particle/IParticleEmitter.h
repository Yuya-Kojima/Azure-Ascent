#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector4.h"
#include "Particle/Particle.h"
#include <string>
#include <list>
#include <random>
#include <wrl.h>

struct EmitterData {
    Vector3 translate;       // 12 bytes
    float padding0;          // 4 bytes  (16 bytes)

    Vector3 spawnArea;       // 12 bytes
    float padding1;          // 4 bytes  (16 bytes)

    uint32_t count;          // 4 bytes
    float frequency;         // 4 bytes
    float frequencyTime;     // 4 bytes
    uint32_t emit;           // 4 bytes  (16 bytes)

    Vector3 baseColor;       // 12 bytes
    float lifeMin;           // 4 bytes  (16 bytes)

    Vector3 baseVelocity;    // 12 bytes
    float lifeMax;           // 4 bytes  (16 bytes)

    Vector3 velocityRandom;  // 12 bytes
    float padding2;          // 4 bytes  (16 bytes)

    Vector3 baseScale;       // 12 bytes
    float padding3;          // 4 bytes  (16 bytes)

    Vector3 scaleRandom;     // 12 bytes
    float padding4;          // 4 bytes  (16 bytes)

    Vector3 baseRotate;      // 12 bytes
    float padding5;          // 4 bytes  (16 bytes)

    Vector3 rotateRandom;    // 12 bytes
    float padding6;          // 4 bytes  (16 bytes)

    Vector3 scaleVelocity;   // 12 bytes
    float isConverge;        // 4 bytes  (16 bytes)
};

struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

struct ParticleEmitDesc {
    Vector3 position;
    uint32_t count;
    Vector3 baseVelocity;
    Vector3 velocityRandom;
    Vector3 spawnRandom;
    float lifeMin;
    float lifeMax;
    Vector4 color;
    Vector3 baseScale = {1.0f, 1.0f, 1.0f};
    Vector3 scaleRandom = {0.0f, 0.0f, 0.0f};

    Vector3 baseRotate = {0.0f, 0.0f, 0.0f};
    Vector3 rotateRandom = {0.0f, 0.0f, 0.0f};

    Vector3 scaleVelocity = {0.0f, 0.0f, 0.0f};
    bool isConverge = false;
};

class IParticleEmitter {
public:
    // 描画時の合成方法（Add: 加算, Alpha: 半透明）
    enum class BlendMode { Add, Alpha };

    IParticleEmitter() = default;
    virtual ~IParticleEmitter();

    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

    // セッター
    void SetTranslate(const Vector3& translate) { emitterData_->translate = translate; }
    void SetSpawnArea(const Vector3& spawnArea) { emitterData_->spawnArea = spawnArea; }
    void SetCount(uint32_t count) { emitterData_->count = count; }
    void SetFrequency(float frequency) { emitterData_->frequency = frequency; }
    void SetEmit(bool emit) { emitterData_->emit = emit ? 1 : 0; }
    
    void SetBaseColor(const Vector3& color) { emitterData_->baseColor = color; }
    void SetLifeTime(float minLife, float maxLife) { emitterData_->lifeMin = minLife; emitterData_->lifeMax = maxLife; }
    void SetBaseVelocity(const Vector3& velocity) { emitterData_->baseVelocity = velocity; }
    void SetVelocityRandom(const Vector3& random) { emitterData_->velocityRandom = random; }
    void SetBaseScale(const Vector3& scale) { emitterData_->baseScale = scale; }
    void SetScaleRandom(const Vector3& random) { emitterData_->scaleRandom = random; }
    
    void SetUVTransform(const Matrix4x4& uvTransform) { materialData_->uvTransform = uvTransform; }
    void SetMaterialColor(const Vector4& color) { materialData_->color = color; }
    void SetIsRingMode(bool isRing) { materialData_->isRingMode = isRing ? 1 : 0; }
    void SetIsConverge(bool isConverge) { emitterData_->isConverge = isConverge ? 1.0f : 0.0f; }

    bool IsEmitting() const { return emitterData_->emit != 0; }
    ID3D12Resource* GetEmitterResource() const { return emitterResource_.Get(); }
    ID3D12Resource* GetGPUParticleResource() const { return gpuParticleResource_.Get(); }
    ID3D12Resource* GetFreeListIndexResource() const { return freeListIndexResource_.Get(); }
    ID3D12Resource* GetFreeListResource() const { return freeListResource_.Get(); }
    uint32_t GetGPUParticleUavIndex() const { return gpuParticleUavIndex_; }

    virtual void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix) = 0;
    virtual void Draw();

    bool needsClear_ = false;

    /// <summary>
    /// 生きている粒をすべて消し、まだ出していない発生の予約も取り消す（実際に消すのは次の ParticleManager::Emit）
    /// </summary>
    void ClearParticles();
    void Emit(const ParticleEmitDesc& desc);
    void Emit(const Vector3& position, uint32_t count,
              const Vector3& baseVelocity = {0.0f, 0.0f, 0.0f},
              const Vector3& velocityRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& spawnRandom = {0.0f, 0.0f, 0.0f},
              float lifeMin = 1.0f, float lifeMax = 1.0f,
              const Vector4& color = {1.0f, 1.0f, 1.0f, 1.0f},
              const Vector3& baseScale = {1.0f, 1.0f, 1.0f},
              const Vector3& scaleRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& baseRotate = {0.0f, 0.0f, 0.0f},
              const Vector3& rotateRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& scaleVelocity = {0.0f, 0.0f, 0.0f});

protected:
    void BaseInitialize(const std::string& textureFilePath);
    void CreateInstancingResource();
    void CreateMaterialResource();

    std::string textureFilePath_;
    uint32_t textureSrvIndex_ = 0;

    BlendMode blendMode_ = BlendMode::Add;

    // GPUパーティクル用のリソース群
    Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource_;
    EmitterData* emitterData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> gpuParticleResource_;
    uint32_t gpuParticleSrvIndex_ = 0;
    uint32_t gpuParticleUavIndex_ = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    struct Material {
        Vector4 color;
        int32_t enableLighting;
        int32_t isBillboard;
        int32_t isRingMode;
        float padding;
        Matrix4x4 uvTransform;
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    const uint32_t kNumMaxInstance_ = 1024; // GPUパーティクルの最大数に合わせる

    std::random_device seedGenerator_;
    std::mt19937 randomEngine_;
};
