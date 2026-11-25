#include "Particle.hlsli"
#include "Particle.CS.hlsl"

StructuredBuffer<Particle> gParticles : register(t0);
ConstantBuffer<PerView> gPerView : register(b0);
ConstantBuffer<Material> gMaterial : register(b1);

struct VertexShaderInput {
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float32_t4x4 MakeRotateXMatrix(float32_t theta) {
    float32_t c = cos(theta);
    float32_t s = sin(theta);
    return float32_t4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, c, s, 0.0f,
        0.0f, -s, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float32_t4x4 MakeRotateYMatrix(float32_t theta) {
    float32_t c = cos(theta);
    float32_t s = sin(theta);
    return float32_t4x4(
        c, 0.0f, -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        s, 0.0f, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float32_t4x4 MakeRotateZMatrix(float32_t theta) {
    float32_t c = cos(theta);
    float32_t s = sin(theta);
    return float32_t4x4(
        c, s, 0.0f, 0.0f,
        -s, c, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

// Z・X・Y の順の回転
float32_t4x4 MakeRotateMatrix(float32_t3 rotate) {
    return mul(MakeRotateZMatrix(rotate.z), mul(MakeRotateXMatrix(rotate.x), MakeRotateYMatrix(rotate.y)));
}

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID) {
    VertexShaderOutput output;
    
    Particle particle = gParticles[instanceId];
    
    float32_t4x4 worldMatrix;
    output.streakPos = float3(0.0f, 0.0f, 1.0f);
    
    if (gMaterial.isBillboard == kBillboardStreak) {
        // 速さから、カメラへ向かう向きの分を除き、画面に沿って動く分だけを残す
        float3 toCamera = normalize(gPerView.cameraPosition - particle.translate);
        float3 screenVelocity = particle.velocity - dot(particle.velocity, toCamera) * toCamera;
        float screenSpeed = length(screenVelocity);
        // 伸びる向き（板の縦）。ほとんど動いていなければ画面の上へ向ける
        float3 along = screenSpeed > 1e-4f ? screenVelocity / screenSpeed : gPerView.billboardMatrix[1].xyz;
        float3 side = normalize(cross(toCamera, along));
        // 線の半分の太さと半分の長さ。長さは、太さの丸い端 2 つに「速さ × 秒数」を足したもの
        float halfWidth = particle.scale.x * gMaterial.streakWidthRate;
        float halfLength = halfWidth + 0.5f * screenSpeed * gMaterial.streakSeconds;
        // 頭の丸い端の中心を粒の位置に置き、尾を飛んできた側へ伸ばす
        float3 center = particle.translate - along * (halfLength - halfWidth);
        worldMatrix = float32_t4x4(
            float4(side * halfWidth, 0.0f),
            float4(along * halfLength, 0.0f),
            float4(toCamera, 0.0f),
            float4(center, 1.0f));
        output.streakPos = float3(input.position.x, input.position.y * halfLength / halfWidth, halfLength / halfWidth);
    } else if (gMaterial.isBillboard != 0) {
        // billboardMatrixを元にworldMatrixを構築
        worldMatrix = gPerView.billboardMatrix;
        if (gMaterial.isBillboard == kBillboardSpin) {
            // 粒の回転を、カメラへ向ける回転より先にかける（板の中で回す）
            worldMatrix = mul(MakeRotateMatrix(particle.rotate), worldMatrix);
        }
        worldMatrix[0] *= particle.scale.x;
        worldMatrix[1] *= particle.scale.y;
        worldMatrix[2] *= particle.scale.z;
        worldMatrix[3].xyz = particle.translate;
    } else {
        // 回転・スケール・平行移動を使った標準のアフィン変換
        float32_t4x4 scaleMatrix = float32_t4x4(
            particle.scale.x, 0.0f, 0.0f, 0.0f,
            0.0f, particle.scale.y, 0.0f, 0.0f,
            0.0f, 0.0f, particle.scale.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        );
        
        float32_t4x4 rotateMatrix = MakeRotateMatrix(particle.rotate);
        
        float32_t4x4 translateMatrix = float32_t4x4(
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            particle.translate.x, particle.translate.y, particle.translate.z, 1.0f
        );
        
        worldMatrix = mul(scaleMatrix, mul(rotateMatrix, translateMatrix));
    }

    float4 worldPosition = mul(input.position, worldMatrix);
    output.position = mul(worldPosition, gPerView.viewProjection);
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    output.texcoord = transformedUV.xy;
    output.color = particle.color;
    // 寿命の進み具合と、炎の模様の原点から見た位置（カメラの右と下の向きに測る）
    output.fireAge = saturate(particle.currentTime / max(particle.lifeTime, 1e-4f));
    float3 fromOrigin = worldPosition.xyz - gMaterial.fireNoiseOrigin;
    output.fireNoisePos = float2(dot(fromOrigin, gPerView.billboardMatrix[0].xyz),
                                 -dot(fromOrigin, gPerView.billboardMatrix[1].xyz));
    
    return output;
}
