#include "CloudSea.hlsli"
#include "Fullscreen.hlsli"

// 元の大きさの深度
Texture2D<float> gDepth : register(t0);
// 半分の解像度で描いた雲（rgb: 雲の光、a: 後ろの光が残る割合）。
// 半分の画素 (i, j) は、元の画素 (2i, 2j) の視線と深度で描いてある
Texture2D<float4> gCloud : register(t4);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

// 奥行きの差（自分の奥行きに対する割合）の下限
static const float kMinDepthDifference = 0.01;

// 深度バッファの値から、カメラから見た奥行き（ビュー空間の Z）を求める
float ViewDepth(float depth) {
	return gCloudSea.projection32 / (depth - gCloudSea.projection22);
}

PixelShaderOutput main(VertexShaderOutput input) {
	PixelShaderOutput output;

	int2 pixel = int2(input.position.xy);
	float viewDepth = ViewDepth(gDepth.Load(int3(pixel, 0)));

	// 自分を囲む半分の解像度の 4 画素と、それぞれへの距離の重み（元の画素 2 つ分が半分の画素 1 つ分）
	int2 base = pixel / 2;
	float2 fraction = float2(pixel - base * 2) * 0.5;
	int2 cloudMax = int2(gCloudSea.screenSize) / 2 - 1;

	float4 sum = 0.0;
	float weightSum = 0.0;
	[unroll]
	for (int k = 0; k < 4; ++k) {
		int2 offset = int2(k & 1, k >> 1);
		int2 cloudPixel = min(base + offset, cloudMax);
		float2 bilinear = lerp(1.0 - fraction, fraction, float2(offset));
		// その雲の画素を描いたときの奥行きが自分と近いほど強く混ぜる
		float sampleDepth = ViewDepth(gDepth.Load(int3(cloudPixel * 2, 0)));
		float depthDifference = abs(viewDepth - sampleDepth) / viewDepth;
		float weight = bilinear.x * bilinear.y / max(depthDifference, kMinDepthDifference);
		sum += gCloud.Load(int3(cloudPixel, 0)) * weight;
		weightSum += weight;
	}

	output.color = weightSum > 0.0 ? sum / weightSum : gCloud.Load(int3(base, 0));
	return output;
}
