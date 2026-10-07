#include "Object3d.hlsli"
#include "Common.hlsli"
#include "Lighting.hlsli"

Texture2D<float4> gTexture : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t1);
Texture2D<float32_t> gMaskTexture : register(t2);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);

struct FogData {
    float4 color;
    float nearDist;
    float farDist;
    float enabled;
    float padding;
};
ConstantBuffer<FogData> gFog : register(b5);
PixelShaderOutput main(
GeometryShaderOutput input) {
	
	//=========================
    // UV transform & sampling
    //=========================
	float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
	float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
	
	//アルファテスト
	if (textureColor.a <= 0.5) {
		discard;
	}

	// ディゾルブ
	float edge = 0.0f;
	if (gMaterial.enableDissolve != 0) {
		float mask = gMaskTexture.Sample(gSampler, input.texcoord + gMaterial.maskTransform).r;
		if (mask <= gMaterial.dissolveThreshold) {
			discard;
		}
		// Edgeっぽさを算出
		edge = 1.0f - smoothstep(gMaterial.dissolveThreshold, gMaterial.dissolveThreshold + gMaterial.dissolveEdgeRange, mask);
	}

	//=========================
    // Base color
    //=========================
	float4 base = gMaterial.color * textureColor;
	
	PixelShaderOutput output;
	
    //=========================
    // No lighting (Emissive / Laser)
    //=========================
	if (gMaterial.enableLighting == 0) {
		float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
		float3 normal = normalize(input.normal);
		// 視線と法線の角度からフレネル係数を計算（中心部ほど大きい）
		float NdotV = saturate(abs(dot(normal, toEye)));

		// 中心コア（純白）と外側（マテリアルカラー）のグラデーション
		float coreFactor = pow(NdotV, 3.0f);
		float3 core = float3(2.5f, 2.5f, 2.5f) * coreFactor;
		float3 aura = base.rgb * (pow(1.0f - NdotV, 0.8f) + 0.4f);

		output.color = float4(core + aura, base.a);
		return output;
	}
	
	//=========================
	// Common vectors
	//=========================
	float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
	float3 normal = normalize(input.normal);
	float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
		
	//=========================
	// Lighting Calculations
	//=========================
	LightResult dirLight = CalculateDirectionalLight(gDirectionalLight, normal, toEye, gMaterial.shininess, baseColor);
	LightResult pointLight = CalculatePointLight(gPointLight, input.worldPosition, normal, toEye, gMaterial.shininess, baseColor);
	LightResult spotLight = CalculateSpotLight(gSpotLight, input.worldPosition, normal, toEye, gMaterial.shininess, baseColor);

	//=========================
	// Output
	//=========================
	output.color.rgb = dirLight.diffuse + dirLight.specular + 
                       pointLight.diffuse + pointLight.specular + 
                       spotLight.diffuse + spotLight.specular;
	
	// Environment Map
	float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
	float3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
	float4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
	output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient;

	// Dissolve Edge 発光
	if (gMaterial.enableDissolve != 0) {
		output.color.rgb += edge * gMaterial.dissolveEdgeColor.rgb;
	}

	// Rim Light（縁の発光。強度0なら無効）
	if (gMaterial.rimColor.w > 0.0f) {
		float rim = pow(saturate(1.0f - saturate(dot(normal, toEye))), max(gMaterial.rimPower, 0.01f));
		output.color.rgb += gMaterial.rimColor.rgb * gMaterial.rimColor.w * rim;
	}

	// Fog
	if (gFog.enabled != 0.0f) {
		float dist = length(gCamera.worldPosition - input.worldPosition);
		float fogFactor = saturate((dist - gFog.nearDist) / (gFog.farDist - gFog.nearDist));
		output.color.rgb = lerp(output.color.rgb, gFog.color.rgb, fogFactor);
	}

	output.color.a = gMaterial.color.a * textureColor.a;
	
	// 計算結果がNaNやInfになったピクセルは描画を破棄する
	if (any(isnan(output.color)) || any(isinf(output.color))) {
		discard;
	}
	
	return output;
}

