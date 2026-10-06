#include "Skybox.hlsli"

TextureCube<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material {
	float4 color;
	float4 skyParams;    // x: mode, y: time
	float4 zenithColor;
	float4 horizonColor;
	float4 sunDir;       // xyz: 方向, w: 雲量
	float4 sunColor;     // rgb: 色, a: 強さ
};

ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

// 3次元Dave Hoskins型ハッシュ（サイン不使用）
float Hash13(float3 p3) {
	p3 = frac(p3 * 0.1031);
	p3 += dot(p3, p3.zyx + 31.32);
	return frac((p3.x + p3.y) * p3.z);
}

// 3次元Value Noise（5次エルミート補間）
float ValueNoise3D(float3 p) {
	float3 i = floor(p);
	float3 f = frac(p);
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

	float n000 = Hash13(i + float3(0, 0, 0));
	float n100 = Hash13(i + float3(1, 0, 0));
	float n010 = Hash13(i + float3(0, 1, 0));
	float n110 = Hash13(i + float3(1, 1, 0));
	float n001 = Hash13(i + float3(0, 0, 1));
	float n101 = Hash13(i + float3(1, 0, 1));
	float n011 = Hash13(i + float3(0, 1, 1));
	float n111 = Hash13(i + float3(1, 1, 1));

	float nx00 = lerp(n000, n100, f.x);
	float nx10 = lerp(n010, n110, f.x);
	float nx01 = lerp(n001, n101, f.x);
	float nx11 = lerp(n011, n111, f.x);

	float nxy0 = lerp(nx00, nx10, f.y);
	float nxy1 = lerp(nx01, nx11, f.y);

	return lerp(nxy0, nxy1, f.z);
}

// 3次元FBM（全天球どこでも歪みや直線の筋が出ない有機的な雲）
float Fbm3D(float3 p) {
	float v = 0.0;
	float a = 0.5;
	const float3x3 rot = float3x3(
		 0.00,  0.80,  0.60,
		-0.80,  0.36, -0.48,
		-0.60, -0.48,  0.64
	);
	for (int i = 0; i < 4; ++i) {
		v += a * ValueNoise3D(p);
		p = mul(rot, p) * 2.02 + float3(17.0, 31.0, 47.0);
		a *= 0.5;
	}
	return v;
}

float3 ProceduralSky(float3 dir) {
	float h = dir.y;
	float3 sunDir = normalize(gMaterial.sunDir.xyz);

	// 空のグラデーション（地平線寄りを広く取り、全周が青空に見えるようにする）
	float t = saturate(h);
	float3 sky = lerp(gMaterial.horizonColor.rgb, gMaterial.zenithColor.rgb, pow(t, 0.55));

	// 地平線より下は地平線色を維持して地面を見せない（雲海が手前に描画される）
	sky = lerp(gMaterial.horizonColor.rgb, sky, step(0.0, h));

	// 太陽のコア + グロー
	float sd = saturate(dot(dir, sunDir));
	float3 sunCol = gMaterial.sunColor.rgb * gMaterial.sunColor.a;
	sky += sunCol * (pow(sd, 3000.0) * 2.5 + pow(sd, 120.0) * 0.20 + pow(sd, 10.0) * 0.08);

	// 球体3Dプロシージャル雲（平面投影を廃止し、あおりや天頂でも切れ目・筋が原理的に出ない）
	float cloudAmount = gMaterial.sunDir.w;
	if (h > 0.02) {
		float time = gMaterial.skyParams.y;
		float3 windOffset = float3(time * 0.015, 0.0, time * 0.010);
		float3 p = dir * 3.2 + windOffset;
		float n = Fbm3D(p);

		float cloudLo = 0.58 - cloudAmount * 0.35;
		float cloud = smoothstep(cloudLo, cloudLo + 0.25, n);

		// 地平線付近は自然にフェードアウト
		cloud *= smoothstep(0.02, 0.20, h);

		// 太陽側は明るく、反対側は空色と馴染ませる
		float3 cloudCol = lerp(gMaterial.horizonColor.rgb * 1.05, float3(1.0, 1.0, 1.0), 0.6) + sunCol * 0.15 * pow(sd, 3.0);
		sky = lerp(sky, cloudCol, cloud * 0.75);
	}
	return sky;
}

PixelShaderOutput main(SkyboxVSOutput input) {
	PixelShaderOutput output;
	if (gMaterial.skyParams.x > 0.5) {
		float3 dir = normalize(input.texcoord);
		output.color = float4(ProceduralSky(dir), 1.0) * gMaterial.color;
	} else {
		float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
		output.color = textureColor * gMaterial.color;
	}
	return output;
}