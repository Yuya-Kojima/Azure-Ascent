#include "Particle.CS.hlsl"

struct PerFrame {
    float32_t time;
    float32_t deltaTime;
};

// 入れ物ごとの、粒にかかる力（C++ の ParticleMotion と同じ並び）
struct ParticleMotion {
    float32_t3 gravity; // 重力の加速度（m/秒²）
    float32_t drag; // 空気の抵抗の強さ（1/秒。重さ 1 の粒が風の速さへ近づく割合）
    float32_t3 wind; // 風の速さ（m/秒）
    float32_t padding0;
};

ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleMotion> gMotion : register(b1);
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

    if (particleIndex < maxParticles) {
        // alphaが0のparticleは死んでいるとみなして更新しない
        if (gParticles[particleIndex].color.a != 0.0f) {
            Particle particle = gParticles[particleIndex];
            float32_t dt = gPerFrame.deltaTime;

            // 速さ: 重力と、風との速さの差に比例する空気の抵抗を受ける
            // 抵抗 ÷ 重さ が、風の速さへ近づく割合（1/秒）。軽い粒ほど早く風に乗る
            float32_t rate = gMotion.drag / max(particle.weight, 1e-4f);
            if (rate > 0.0f) {
                // 重力と抵抗がつり合って落ち着く速さ（終端速度）
                float32_t3 terminal = gMotion.wind + gMotion.gravity / rate;
                // 終端速度との差を、dt 秒ぶん exp で縮める（運動方程式の正確な解）
                particle.velocity = terminal + (particle.velocity - terminal) * exp(-rate * dt);
            } else {
                particle.velocity += gMotion.gravity * dt;
            }
            particle.translate += particle.velocity * dt;
            particle.rotate += particle.rotateVelocity * dt;
            particle.currentTime += dt;

            // 寿命の割合（0 で生まれた・1 で消える）
            float32_t ratio = saturate(particle.currentTime / particle.lifeTime);

            // 色: 始まりの色から終わりの色へ
            particle.color.rgb = lerp(particle.colorStart, particle.colorEnd, ratio);

            // 大きさ: 一定の速さの増減に、始まり 1 → 終わり scaleEndRate の倍率をかける
            // 倍率は寿命の初めに大きく変わり、終わりへ向けてゆっくり落ち着く（1 - (1 - 割合)²）
            float32_t3 scale = particle.startScale + particle.scaleVelocity * particle.currentTime;
            float32_t scaleProgress = 1.0f - (1.0f - ratio) * (1.0f - ratio);
            particle.scale = max(float32_t3(0.0f, 0.0f, 0.0f), scale * lerp(1.0f, particle.scaleEndRate, scaleProgress));

            // 透明度: fadeStart までは 1、そこから寿命の終わりで 0
            float32_t fadeLength = max(1.0f - particle.fadeStart, 1e-4f);
            particle.color.a = 1.0f - saturate((ratio - particle.fadeStart) / fadeLength);

            // 寿命が尽きたかは時間で決め、尽きたら透明度を 0 にそろえる
            bool isDead = particle.currentTime >= particle.lifeTime;
            if (isDead) {
                particle.color.a = 0.0f;
            }
            gParticles[particleIndex] = particle;

            // 寿命が尽きたので、ここはFreeとする
            if (isDead) {
                // スケールに0を入れておいてVertexShader出力で棄却されるようにする
                gParticles[particleIndex].scale = float32_t3(0.0f, 0.0f, 0.0f);
                int32_t freeListIndex;
                InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
                // 最新のFreeListIndexの場所に死んだParticleのIndexを設定する。
                if ((freeListIndex + 1) < maxParticles) {
                    gFreeList[freeListIndex + 1] = particleIndex;
                } else {
                    // ここにくるはずはない。きたら何かが間違っているが、安全策をうっておく
                    InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
                }
            }
        }
    }
}
