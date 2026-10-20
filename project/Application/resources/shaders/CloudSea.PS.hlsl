#include "CloudSea.hlsli"
#include "Lighting.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct FogData {
	float4 color;
	float nearDist;
	float farDist;
	float enabled;
	float padding;
};
ConstantBuffer<FogData> gFog : register(b2);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

// 裂け目の穴の下に見える崖を描かない深さ（起伏の一番低い所からさらに下）
static const float kCliffCutDepth = 1.5;
// ひびの筋の細さ（ノイズの値が 0.5 からこの幅の中なら筋）
static const float kCrackLineWidth = 0.035;
// ひびの筋の暗さ（1 で真っ黒）
static const float kCrackDarkness = 0.75;

// 盛り上がりの上のひびの濃さ（0〜1）
float CrackAmount(float2 xz) {
	float crack = 0.0;
	for (int i = 0; i < gCloudSea.shapeCount; ++i) {
		CloudSeaShape s = gCloudSea.shapes[i];
		if (s.kind != kShapeSwell || s.crack <= 0.0) {
			continue;
		}
		float d = length(xz - s.center) / max(s.radius, 1e-3);
		crack = max(crack, s.crack * exp(-d * d));
	}
	if (crack <= 0.0) {
		return 0.0;
	}
	// ノイズの値がちょうど 0.5 になる線をつないで、枝分かれするひびの筋にする
	float n = ValueNoise2D(xz * 0.45) * 0.6 + ValueNoise2D(xz * 1.1 + 5.0) * 0.4;
	float crackLine = 1.0 - smoothstep(0.0, kCrackLineWidth, abs(n - 0.5));
	return saturate(crack) * crackLine;
}

PixelShaderOutput main(CloudSeaVSOutput input) {
	PixelShaderOutput output;
	float2 xz = input.worldPosition.xz;

	// 裂け目の穴の内側と、その下の崖は描かない
	if (input.worldPosition.y < gCloudSea.height - gCloudSea.amplitude - kCliffCutDepth) {
		discard;
	}
	for (int i = 0; i < gCloudSea.shapeCount; ++i) {
		CloudSeaShape s = gCloudSea.shapes[i];
		if (s.kind == kShapeTear && s.amount > 0.0) {
			float2 toPoint = xz - s.center;
			if (length(toPoint) < CloudSeaTearEdge(s, toPoint)) {
				discard;
			}
		}
	}

	float3 normal = normalize(input.normal);
	float3 toEye = normalize(gCloudSea.cameraPosition - input.worldPosition);

	// 模様（風で流す）
	float2 uv = (xz - gCloudSea.wind * gCloudSea.time) * gCloudSea.textureScale;
	float3 textureColor = gTexture.Sample(gSampler, uv).rgb;

	// 調べるための表示（平行光源の debugView。1: テクスチャの色だけ, 2: 法線, 4: 光だけ）
	if (gDirectionalLight.debugView == 1) {
		output.color = float4(gCloudSea.color.rgb * textureColor, 1.0);
		return output;
	}
	if (gDirectionalLight.debugView == 2) {
		output.color = float4(normal * 0.5 + 0.5, 1.0);
		return output;
	}
	if (gDirectionalLight.debugView == 4) {
		textureColor = float3(1.0, 1.0, 1.0);
	}

	float3 baseColor = gCloudSea.color.rgb * textureColor;

	// ほかの物と同じ光（ハーフランバートの平行光源＋上下の環境光）
	LightResult light = CalculateDirectionalLight(gDirectionalLight, normal, toEye, 1.0, baseColor);
	float3 color = light.diffuse + CalculateHemisphereAmbient(gDirectionalLight, normal, baseColor);

	// 谷ほど暗く（起伏の一番低い所で 1 - valleyDarkness 倍、一番高い所で 1 倍）
	float amplitude = max(gCloudSea.amplitude, 1e-3);
	float heightRatio = saturate((input.baseHeight + amplitude) / (2.0 * amplitude));
	color *= 1.0 - gCloudSea.valleyDarkness * (1.0 - heightRatio);

	// 盛り上がりの上のひび
	color *= 1.0 - kCrackDarkness * CrackAmount(xz);

	// 逆光の縁（視線に対して横を向いた面ほど、カメラが光の方を向いているほど強い。強さ 0 で無効）
	if (gCloudSea.rimStrength > 0.0) {
		float fresnel = pow(saturate(1.0 - saturate(dot(normal, toEye))), max(gCloudSea.rimPower, 0.01));
		float facing = dot(-toEye, -gDirectionalLight.backRimDirection) * 0.5 + 0.5;
		facing *= facing;
		facing *= facing;
		color += gDirectionalLight.backRimColor.rgb * gCloudSea.rimStrength * fresnel * facing;
	}

	// 霧（ほかの物と同じ直線の霧）
	if (gFog.enabled != 0.0) {
		float dist = length(gCloudSea.cameraPosition - input.worldPosition);
		float fogFactor = saturate((dist - gFog.nearDist) / (gFog.farDist - gFog.nearDist));
		color = lerp(color, gFog.color.rgb, fogFactor);
	}

	output.color = float4(color, 1.0);
	return output;
}
