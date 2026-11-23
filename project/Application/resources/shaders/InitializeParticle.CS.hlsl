#include "Particle.CS.hlsl"

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);

[numthreads(1024, 1, 1)]
void main(uint32_t3 DTid : SV_DispatchThreadID) {
    uint32_t particleIndex = DTid.x;

    // 粒の上限は、バッファの要素の数から読む
    uint32_t maxParticles;
    uint32_t stride;
    gParticles.GetDimensions(maxParticles, stride);
    
    if (particleIndex == 0) {
        gFreeListIndex[0] = maxParticles - 1;
    }

    if (particleIndex < maxParticles) {
        // Particle構造体の全要素を0で埋める
        gParticles[particleIndex] = (Particle)0;
        // FreeListを連番で初期化
        gFreeList[particleIndex] = particleIndex;
    }
}
