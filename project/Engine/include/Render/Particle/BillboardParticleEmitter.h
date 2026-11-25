#pragma once
#include "Particle/IParticleEmitter.h"

class BillboardParticleEmitter : public IParticleEmitter {
public:
    BillboardParticleEmitter() = default;
    ~BillboardParticleEmitter() override = default;

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="textureFilePath">粒の絵</param>
    /// <param name="maxParticles">同時に生きていられる粒の上限</param>
    void Initialize(const std::string& textureFilePath,
                    uint32_t maxParticles = kDefaultMaxParticles);

    void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix) override;

private:
    void CreateVertexData();
};
