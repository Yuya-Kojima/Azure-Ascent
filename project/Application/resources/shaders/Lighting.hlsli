#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

//=========================
// ライト構造体の定義
//=========================
struct DirectionalLight {
	float4 color;
	float3 direction;
	float intensity;
	float4 skyAmbient;     // 法線が上を向くときの環境光
	float4 groundAmbient;  // 法線が下を向くときの環境光
	float4 backRimColor;   // 逆光の縁: rgb=色, w=強度
	float backRimPower;
	float3 backRimDirection; // 逆光の縁を作る光が進む向き
	int debugView;           // 調べるための表示（0: 通常, 1: テクスチャの色だけ, 2: 法線, 3: 法線の向き, 4: 光だけ, 5: 壊れた法線）
	float3 padding;
};

struct PointLight {
	float4 color;
	float3 position;
	float intensity;
	float radius;
	float decay;
};

struct SpotLight {
	float4 color;
	float3 position;
	float intensity;
	float3 direction;
	float distance;
	float decay;
	float cosAngle;
};

struct LightResult {
    float3 diffuse;
    float3 specular;
};

//=========================
// ライティング計算関数
//=========================

// 平行光源の計算
LightResult CalculateDirectionalLight(DirectionalLight light, float3 normal, float3 toEye, float shininess, float3 baseColor) {
    LightResult result;
    
    // ハーフランバート (Diffuse)
    float NdotL = dot(normal, -light.direction);
    float cos = pow(NdotL * 0.5f + 0.5f, 2.0f);
    result.diffuse = baseColor * light.color.rgb * cos * light.intensity;
    
    // Blinn-Phong (Specular)
    float3 halfVector = normalize(-light.direction + toEye);
    float NDotH = dot(normal, halfVector);
    float specularPow = pow(saturate(NDotH), shininess);
    result.specular = light.color.rgb * light.intensity * specularPow;
    
    return result;
}

// 環境光（法線の上下で空の色と照り返しの色を混ぜる）
float3 CalculateHemisphereAmbient(DirectionalLight light, float3 normal, float3 baseColor) {
    float up = normal.y * 0.5f + 0.5f;
    return baseColor * lerp(light.groundAmbient.rgb, light.skyAmbient.rgb, up);
}

// 逆光の縁（視線に対して横を向いた面ほど強く、カメラが光の方を向いているほど強い）
float3 CalculateBackRim(DirectionalLight light, float3 normal, float3 toEye, float scale) {
    float fresnel = pow(saturate(1.0f - saturate(dot(normal, toEye))), max(light.backRimPower, 0.01f));
    // 視線の向き（-toEye）と光の来る向き（-backRimDirection）が一致すると 1、直交で 0.25、逆向きで 0
    float facing = dot(-toEye, -light.backRimDirection) * 0.5f + 0.5f;
    facing *= facing;
    return light.backRimColor.rgb * light.backRimColor.w * scale * fresnel * facing;
}

// 点光源の計算
LightResult CalculatePointLight(PointLight light, float3 worldPosition, float3 normal, float3 toEye, float shininess, float3 baseColor) {
    LightResult result;
    result.diffuse = float3(0.0f, 0.0f, 0.0f);
    result.specular = float3(0.0f, 0.0f, 0.0f);

    float distance = length(light.position - worldPosition);
    float factor = pow(saturate(-distance / light.radius + 1.0f), light.decay);
    if (factor <= 0.0f) return result;

    float3 lightDir = normalize(worldPosition - light.position);
    float3 lightColor = light.color.rgb * light.intensity * factor;

    // Diffuse
    float NdotL = dot(normal, -lightDir);
    float cosPoint = pow(NdotL * 0.5f + 0.5f, 2.0f);
    result.diffuse = baseColor * lightColor * cosPoint;

    // Specular
    float3 halfVector = normalize(-lightDir + toEye);
    float NdotH = dot(normal, halfVector);
    float specularPow = pow(saturate(NdotH), shininess);
    result.specular = lightColor * specularPow;

    return result;
}

// スポットライトの計算
LightResult CalculateSpotLight(SpotLight light, float3 worldPosition, float3 normal, float3 toEye, float shininess, float3 baseColor) {
    LightResult result;
    result.diffuse = float3(0.0f, 0.0f, 0.0f);
    result.specular = float3(0.0f, 0.0f, 0.0f);

    float3 lightDir = normalize(worldPosition - light.position);
    float cosAngle = dot(lightDir, light.direction);
    float falloffFactor = saturate((cosAngle - light.cosAngle) / (1.0f - light.cosAngle));
    if (falloffFactor <= 0.0f) return result;

    float spotDistance = length(light.position - worldPosition);
    float attenuationFactor = pow(saturate(-spotDistance / light.distance + 1.0f), light.decay);
    if (attenuationFactor <= 0.0f) return result;

    float3 lightColor = light.color.rgb * light.intensity * attenuationFactor * falloffFactor;

    // Diffuse
    float NdotL = dot(normal, -lightDir);
    float cosSpot = pow(NdotL * 0.5f + 0.5f, 2.0f);
    result.diffuse = baseColor * lightColor * cosSpot;

    // Specular
    float3 halfVector = normalize(-lightDir + toEye);
    float NdotH = dot(normal, halfVector);
    float specularPow = pow(saturate(NdotH), shininess);
    result.specular = lightColor * specularPow;

    return result;
}

#endif
