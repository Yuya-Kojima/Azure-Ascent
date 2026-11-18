#include "Skybox.hlsli"
#include "SkyClearing.hlsli"

TextureCube<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material {
	float4 color;
	float4 skyParams;    // x: mode, y: time, z: 雲の縁の明るさ, w: 確認用の表示（0: ふつう, 1: 向き, 2: 雲のノイズ, 3: 晴れの割合）
	float4 zenithColor;
	float4 horizonColor;
	float4 sunDir;       // xyz: 方向, w: 雲量
	float4 sunColor;     // rgb: 色, a: 強さ
	float4 cloudColor;   // rgb: 雲の色, a: 雲の不透明度（0なら白い雲）
	float4 horizonGlow;  // x: 地平線の光の強さ, y: 光の帯の高さ（dir.y の単位）, z: 方位の絞り（大きいほど太陽の左右だけ）

	// 晴れた空の値（並びは上と同じ。clearHorizonGlow.w は雲の縁の明るさ）
	float4 clearZenithColor;
	float4 clearHorizonColor;
	float4 clearSunDir;
	float4 clearSunColor;
	float4 clearCloudColor;
	float4 clearHorizonGlow;
	float4 clearCircle;  // xyz: 晴れの円の中心の向き, w: 円の半径（ラジアン。0 以下で晴れなし）
	float4 clearEdge;    // x: 境目のぼかしの幅, y: 外側で闇が濃くなる帯の幅, z: 濃くなる強さ, w: 境目をノイズで揺らす幅（ラジアン）
};

ConstantBuffer<Material> gMaterial : register(b0);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

// 空 1 組ぶんの値
struct SkyPalette {
	float3 zenith;
	float3 horizon;
	float3 sunDir;
	float cloudAmount;
	float3 sunColor;
	float sunIntensity;
	float3 cloudColor;
	float cloudOpacity;
	float cloudRim;
	float3 glow; // x: 地平線の光の強さ, y: 光の帯の高さ, z: 方位の絞り
};

SkyPalette DarkPalette() {
	SkyPalette p;
	p.zenith = gMaterial.zenithColor.rgb;
	p.horizon = gMaterial.horizonColor.rgb;
	p.sunDir = normalize(gMaterial.sunDir.xyz);
	p.cloudAmount = gMaterial.sunDir.w;
	p.sunColor = gMaterial.sunColor.rgb;
	p.sunIntensity = gMaterial.sunColor.a;
	p.cloudColor = gMaterial.cloudColor.rgb;
	p.cloudOpacity = gMaterial.cloudColor.a;
	p.cloudRim = gMaterial.skyParams.z;
	p.glow = gMaterial.horizonGlow.xyz;
	return p;
}

SkyPalette ClearPalette() {
	SkyPalette p;
	p.zenith = gMaterial.clearZenithColor.rgb;
	p.horizon = gMaterial.clearHorizonColor.rgb;
	p.sunDir = normalize(gMaterial.clearSunDir.xyz);
	p.cloudAmount = gMaterial.clearSunDir.w;
	p.sunColor = gMaterial.clearSunColor.rgb;
	p.sunIntensity = gMaterial.clearSunColor.a;
	p.cloudColor = gMaterial.clearCloudColor.rgb;
	p.cloudOpacity = gMaterial.clearCloudColor.a;
	p.cloudRim = gMaterial.clearHorizonGlow.w;
	p.glow = gMaterial.clearHorizonGlow.xyz;
	return p;
}

// 雲のノイズ（dir の向きの雲の濃さのもとになる値。両方の空で同じ値を使う）
float CloudNoise(float3 dir) {
	return SkyCloudNoise(dir, gMaterial.skyParams.y);
}

// 空 1 組ぶんの色。n は CloudNoise の値
float3 ProceduralSky(float3 dir, SkyPalette p, float n) {
	float h = dir.y;
	float3 sunDir = p.sunDir;

	// 空のグラデーション（地平線寄りを広く取り、全周が青空に見えるようにする）
	float t = saturate(h);
	float3 sky = lerp(p.horizon, p.zenith, pow(t, 0.55));

	// 地平線より下は地平線色を維持して地面を見せない（雲海が手前に描画される）
	sky = lerp(p.horizon, sky, step(0.0, h));

	// 太陽のコア + グロー
	float sd = saturate(dot(dir, sunDir));
	float3 sunCol = p.sunColor * p.sunIntensity;
	sky += sunCol * (pow(sd, 3000.0) * 2.5 + pow(sd, 120.0) * 0.20 + pow(sd, 10.0) * 0.08);

	// 地平線の光：太陽の方位で、地平線のすぐ上だけを帯状に光らせる（強さ0なら無効）
	float glowStrength = p.glow.x;
	if (glowStrength > 0.0) {
		float2 dirXZ = normalize(dir.xz + float2(1e-5, 0.0));
		float2 sunXZ = normalize(sunDir.xz + float2(1e-5, 0.0));
		float azimuth = pow(saturate(dot(dirXZ, sunXZ)), max(p.glow.z, 0.01));
		float band = exp(-max(h, 0.0) / max(p.glow.y, 0.001));
		sky += p.sunColor * glowStrength * azimuth * band;
	}

	// 球体3Dプロシージャル雲（平面投影を廃止し、あおりや天頂でも切れ目・筋が原理的に出ない）
	if (h > 0.02) {
		float cloudLo = 0.58 - p.cloudAmount * 0.35;
		float cloud = smoothstep(cloudLo, cloudLo + 0.25, n);

		// 雲の色が指定されているか
		bool useCloudColor = p.cloudOpacity > 0.0;

		// 地平線付近は自然にフェードアウト（雲の色を指定したときは地平線の近くまで雲を残す）
		cloud *= smoothstep(0.02, useCloudColor ? 0.08 : 0.20, h);

		if (useCloudColor) {
			// 指定色の雲。濃さが中間の縁（cloud * (1 - cloud) が最大）ほど、太陽側で太陽の色に光る
			float rim = cloud * (1.0 - cloud) * 4.0;
			float3 cloudCol = p.cloudColor +
				sunCol * (p.cloudRim * rim * (0.2 + 0.8 * pow(sd, 4.0)) + 0.15 * pow(sd, 3.0));
			sky = lerp(sky, cloudCol, cloud * p.cloudOpacity);
		} else {
			// 太陽側は明るく、反対側は空色と馴染ませる
			float3 cloudCol = lerp(p.horizon * 1.05, float3(1.0, 1.0, 1.0), 0.6) + sunCol * 0.15 * pow(sd, 3.0);
			sky = lerp(sky, cloudCol, cloud * 0.75);
		}
	}
	return sky;
}

// 淀んだ空と晴れた空を、晴れの円で塗り分けた色
// 円の中心からの角度が半径より内側は晴れた空。境目はノイズで揺らしてぼかし、すぐ外側の帯では淀んだ空を暗くする
float3 SkyWithClearing(float3 dir) {
	float n = CloudNoise(dir);
	float radius = gMaterial.clearCircle.w;
	if (radius <= 0.0) {
		return ProceduralSky(dir, DarkPalette(), n);
	}

	// 境目から外側へ測った角度（内側で負）。境目を雲のノイズで内外へ揺らし、まん丸に見せない
	float outside = ClearCircleOutside(dir, gMaterial.clearCircle, gMaterial.clearEdge.w, n);
	float clearRatio = ClearCircleRatio(outside, gMaterial.clearEdge.x);
	if (clearRatio >= 1.0) {
		return ProceduralSky(dir, ClearPalette(), n);
	}

	float3 dark = ProceduralSky(dir, DarkPalette(), n);
	// 押しやられた闇：境目に近いほど暗く、外側 clearEdge.y の幅で元の暗さへ戻る
	dark *= 1.0 - gMaterial.clearEdge.z * ClearCirclePush(outside, gMaterial.clearEdge.y);
	if (clearRatio <= 0.0) {
		return dark;
	}
	float3 clear = ProceduralSky(dir, ClearPalette(), n);
	return lerp(dark, clear, clearRatio);
}

PixelShaderOutput main(SkyboxVSOutput input) {
	PixelShaderOutput output;
	if (gMaterial.skyParams.x > 0.5) {
		float3 dir = normalize(input.texcoord);
		output.color = float4(SkyWithClearing(dir), 1.0) * gMaterial.color;
		// 確認用の表示：向き・雲のノイズ・晴れの割合を色で出す
		int debugView = (int)(gMaterial.skyParams.w + 0.5);
		if (debugView == 1) {
			output.color = float4(dir * 0.5 + 0.5, 1.0);
		} else if (debugView == 2) {
			output.color = float4(CloudNoise(dir).xxx, 1.0);
		} else if (debugView == 3) {
			float outside = ClearCircleOutside(dir, gMaterial.clearCircle, gMaterial.clearEdge.w, CloudNoise(dir));
			float clearRatio = ClearCircleRatio(outside, gMaterial.clearEdge.x);
			output.color = float4(clearRatio.xxx, 1.0);
		}
	} else {
		float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
		output.color = textureColor * gMaterial.color;
	}
	return output;
}