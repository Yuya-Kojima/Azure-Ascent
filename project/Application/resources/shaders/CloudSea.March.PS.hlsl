#include "CloudSea.hlsli"
#include "Fullscreen.hlsli"
#include "Lighting.hlsli"

// 不透明な物を描いたあとの元の大きさの深度（物の手前で視線を止める）
Texture2D<float> gDepth : register(t0);

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
	// rgb: 雲から目に届く光、a: 雲の後ろの物の光が残る割合（半分の解像度のテクスチャへ書く）
	float4 color : SV_TARGET0;
};

static const float kPi = 3.14159265;

// 視線の歩幅（近くで細かく、遠くで粗く）：kMinStep ＋ 距離 × kStepPerDistance、上限 kMaxStep
static const float kMinStep = 0.2;
static const float kStepPerDistance = 0.012;
static const float kMaxStep = 8.0;
// 1 本の視線で濃さを測る回数の上限
static const int kMaxSteps = 256;
// 後ろの光がこれより残らなくなったら止める
static const float kMinTransmittance = 0.02;
// 上の面の傾きの上限のめやす（上の面より上を飛ばすときに使う）
static const float kMaxTopSlope = 1.5;
// 上の面より下の歩幅：縁の厚み（edgeSoftness）× kInsideStepPerSoftness、下限 kMinInsideStep
static const float kInsideStepPerSoftness = 0.25;
static const float kMinInsideStep = 0.05;

// 太陽の光の向きへ濃さを測る回数と、1 回目の歩幅（2 回目からは前の倍）
static const int kLightSteps = 6;
static const float kLightFirstStep = 0.3;
// 地平線の光の向きへ濃さを測る回数と、1 回目の歩幅（2 回目からは前の倍）
static const int kRimLightSteps = 5;
static const float kRimLightFirstStep = 0.5;

// 前へ散る光の偏り（Henyey-Greenstein の g）
static const float kForwardScatterG = 0.6;

// Henyey-Greenstein の位相関数を、全方向へ同じに散るときを 1 として表した値
float HenyeyGreenstein(float cosTheta, float g) {
	float g2 = g * g;
	return (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5);
}

// 画素ごとに 0〜1 の値（Interleaved Gradient Noise）
float InterleavedGradientNoise(float2 pixel) {
	return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// 画面の位置（NDC）と深度から、ワールドの位置を戻す
float3 ReconstructWorldPosition(float2 ndc, float depth) {
	float4 world = mul(float4(ndc, depth, 1.0), gCloudSea.inverseViewProjection);
	return world.xyz / world.w;
}

// p から光の来る向きへ進んだ間の、光の弱まり（光学的な厚さ）。歩幅は firstStep から 1 回ごとに倍
float LightOpticalDepth(float3 p, float3 toLight, float maxTop, int steps, float firstStep) {
	float optical = 0.0;
	float travelled = 0.0;
	float step = firstStep;
	for (int i = 0; i < steps; ++i) {
		float3 q = p + toLight * (travelled + step * 0.5);
		if (q.y > maxTop) {
			break;
		}
		optical += CloudSeaDensity(q, CloudSeaTopHeight(q.xz), false) * step;
		travelled += step;
		step *= 2.0;
	}
	return optical * gCloudSea.density;
}

// 雲の 1 点が目の向きへ返す光
float3 InScatteredLight(float3 p, float sampleDensity, float top, float3 rayDir, float maxTop) {
	// 太陽の光（平行光源）：雲の中を通る間の弱まり（Beer の法則）と粉の暗さ（Beer-Powder）
	float3 toLight = normalize(-gDirectionalLight.direction);
	float lightDepth = LightOpticalDepth(p, toLight, maxTop, kLightSteps, kLightFirstStep);
	float beer = exp(-lightDepth);
	// 表面のすぐ内側ほど、光が中まで届いておらず暗い（光の向きの厚さが小さい所ほど 0 に近い）
	float powder = 1.0 - exp(-2.0 * (lightDepth + sampleDensity * gCloudSea.density * 0.5));
	float sunEnergy = beer * lerp(1.0, powder * 2.0, gCloudSea.powder);
	// 光の進む向きと目へ向かう向きの角度で、前へ散る分を混ぜる
	float cosTheta = dot(gDirectionalLight.direction, -rayDir);
	sunEnergy *= lerp(1.0, HenyeyGreenstein(cosTheta, kForwardScatterG), gCloudSea.forwardScatter);
	float3 sun = gDirectionalLight.color.rgb * gDirectionalLight.intensity * sunEnergy;

	// 上下の環境光：上の面が高い所ほど空の色、低い所（谷・層の中）ほど照り返しの色で暗い
	float amplitude = max(gCloudSea.amplitude, 1e-3);
	float heightRatio = saturate((p.y - (gCloudSea.height - amplitude)) / (2.0 * amplitude));
	float3 ambient = lerp(gDirectionalLight.groundAmbient.rgb, gDirectionalLight.skyAmbient.rgb, 0.5 + 0.5 * heightRatio);
	ambient *= 1.0 - gCloudSea.valleyDarkness * (1.0 - heightRatio);

	if (gCloudSea.debugView == 1) {
		ambient = 0.0;
	} else if (gCloudSea.debugView == 2) {
		sun = 0.0;
	} else if (gCloudSea.debugView == 3) {
		return float3(1.0, 1.0, 1.0);
	}

	float3 light = gCloudSea.color.rgb * gCloudSea.brightness * (sun + ambient);

	// 地平線の光：光の来る向きへ雲が薄い所ほど透けて光り、ほかの塊の陰では暗い（強さ 0 で無効）。
	// 透けるのは上の面のすぐ下だけ（上の面からの深さが edgeSoftness で 1/e）
	if (gCloudSea.rimStrength > 0.0) {
		float3 toRim = normalize(-gDirectionalLight.backRimDirection);
		float rimDepth = LightOpticalDepth(p, toRim, maxTop, kRimLightSteps, kRimLightFirstStep);
		float rimCos = dot(gDirectionalLight.backRimDirection, -rayDir);
		float rimG = saturate(1.0 - 1.0 / max(gCloudSea.rimPower, 1.0));
		float nearTop = exp(-max(top - p.y, 0.0) / max(gCloudSea.edgeSoftness, 1e-3));
		light += gDirectionalLight.backRimColor.rgb * gCloudSea.rimStrength * HenyeyGreenstein(rimCos, rimG) * exp(-rimDepth) * nearTop;
	}
	return light;
}

PixelShaderOutput main(VertexShaderOutput input) {
	PixelShaderOutput output;

	// 半分の解像度の画素 (i, j) は、元の画素 (2i, 2j) の視線と深度で描く
	int2 fullPixel = int2(input.position.xy) * 2;
	float2 uv = (float2(fullPixel) + 0.5) / gCloudSea.screenSize;
	float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	float3 cameraPosition = gCloudSea.cameraPosition;
	float3 rayDir = normalize(ReconstructWorldPosition(ndc, 1.0) - cameraPosition);

	// 物があればその手前まで（空は深度 1 のまま）
	float depth = gDepth.Load(int3(fullPixel, 0));
	bool hitObject = depth < 1.0;
	float objectDistance = hitObject ? length(ReconstructWorldPosition(ndc, depth) - cameraPosition) : 1e9;

	// 層（底〜上の面の一番高い所）の中を通る区間
	float maxTop = CloudSeaMaxTopHeight();
	float bottom = CloudSeaBottomHeight();
	float tStart = 0.0;
	float tEnd = 1e9;
	if (abs(rayDir.y) > 1e-5) {
		float tBottom = (bottom - cameraPosition.y) / rayDir.y;
		float tTop = (maxTop - cameraPosition.y) / rayDir.y;
		tStart = max(min(tBottom, tTop), 0.0);
		tEnd = max(tBottom, tTop);
	} else if (cameraPosition.y < bottom || cameraPosition.y > maxTop) {
		tEnd = 0.0;
	}
	// 霧の距離より遠くは描かない
	float fogFar = gFog.enabled != 0.0 ? gFog.farDist : 1e9;
	tEnd = min(tEnd, min(objectDistance, fogFar));

	float3 scattered = 0.0;
	float transmittance = 1.0;

	float t = tStart + (kMinStep + tStart * kStepPerDistance) * InterleavedGradientNoise(input.position.xy) * gCloudSea.jitter;
	int stepCount = 0;
	for (; stepCount < kMaxSteps && t < tEnd; ++stepCount) {
		float3 p = cameraPosition + rayDir * t;
		float top = CloudSeaTopHeight(p.xz);
		float step = min(kMinStep + t * kStepPerDistance, kMaxStep);

		// 上の面より上：上の面に届きうる所まで一気に進む（上の面を kMinStep より深く越えない）
		if (p.y > top) {
			float approach = max(-rayDir.y, 0.0) + kMaxTopSlope * length(rayDir.xz);
			t += max((p.y - top) / max(approach, 1e-4), kMinStep);
			continue;
		}

		// 上の面より下：縁の厚みより細かい歩幅で進む
		step = min(step, max(gCloudSea.edgeSoftness * kInsideStepPerSoftness, kMinInsideStep));
		step = min(step, tEnd - t);
		float sampleDensity = CloudSeaDensity(p, top, true);
		if (sampleDensity > 0.0) {
			float3 light = InScatteredLight(p, sampleDensity, top, rayDir, maxTop);
			// 霧（ほかの物と同じ直線の霧）
			if (gFog.enabled != 0.0) {
				float fogFactor = saturate((t - gFog.nearDist) / (gFog.farDist - gFog.nearDist));
				light = lerp(light, gFog.color.rgb, fogFactor);
			}
			// 歩幅で光の量が変わらない積分：この区間で弱まった分だけ、雲の光に置き換わる
			float stepTransmittance = exp(-sampleDensity * gCloudSea.density * step);
			scattered += transmittance * light * (1.0 - stepTransmittance);
			transmittance *= stepTransmittance;
			if (transmittance < kMinTransmittance) {
				break;
			}
		}
		t += step;
	}

	// 下向きの視線で後ろが空のとき（霧の距離より遠くの雲・層の底より下）は、霧の色にする
	if (!hitObject && rayDir.y < 0.0) {
		scattered += transmittance * gFog.color.rgb;
		transmittance = 0.0;
	}

	// 調べるための表示：濃さを測った回数を白黒で、上限で打ち切った所を赤で出す
	if (gCloudSea.debugView == 4) {
		bool exhausted = stepCount >= kMaxSteps && t < tEnd && transmittance >= kMinTransmittance;
		float ratio = float(stepCount) / float(kMaxSteps);
		output.color = float4(exhausted ? float3(4.0, 0.0, 0.0) : float3(ratio, ratio, ratio) * 2.0, 0.0);
		return output;
	}

	output.color = float4(scattered, transmittance);
	return output;
}
