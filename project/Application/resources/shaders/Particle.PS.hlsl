#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b1);

// 炎の描き方
// 模様は空間（炎の模様の原点から見た位置）に置いて時間で上へ流す。粒は「どこが燃えているか」の形だけを受け持つ
static const float kFireNoiseWorldScale = 0.05f; // 模様の画像 1 枚が覆う長さの逆数（1/m）
static const float kFireFlowSpeed = 0.3f;        // 模様を上へ流す速さ（画像の大きさ/秒）
static const float kFireDetailScale = 2.3f;      // 細かい模様の、粗い模様に対する細かさ
static const float kFireDetailWeight = 0.4f;     // 細かい模様の混ぜる割合
static const float2 kFireShapeCenter = float2(0.5f, 0.62f); // 炎の形の中心（UV。下が 1）
static const float kFireShapeRadius = 0.45f;   // 炎の形の半径（UV）
static const float kFireStretchUp = 0.5f;      // 中心より上の縦の縮め（1 より小さいと上へ長く伸びる）
static const float kFireStretchDown = 1.6f;    // 中心より下の縦の縮め（1 より大きいと下は早く薄れる）
static const float kFireTaper = 1.6f;          // 上へ行くほど細くする強さ
static const float kFireErodeStart = 0.05f;    // 生まれたときに削るしきい値
static const float kFireErodeSoft = 0.3f;      // 削る境目のぼかし幅
static const float kFireBallRadius = 0.5f;     // 火の玉の形の半径（UV）
static const float kFireTemperatureScale = 1.6f; // 「濃さ × 残りの寿命の 2 乗」を温度にする倍率（燃え続ける炎）
static const float kFireBallTemperatureScale = 1.2f; // 同じ倍率（火の玉）
static const float kFireColdFade = 3.0f;       // 温度の低い所を薄くする強さ
static const float kFireDimIntensity = 0.5f;   // 温度 0 の所の明るさの倍率
static const float kFireHotIntensity = 4.0f;   // 温度 1 の所の明るさの倍率（1 を超えた分がブルームでにじむ）
static const float kFireOcclusion = 0.1f;      // 炎のある所で後ろを隠す割合（0 で足し算、1 で半透明）
// 温度の色の帯（冷たい → 熱い）
static const float3 kFireRed = float3(0.6f, 0.08f, 0.02f);
static const float3 kFireOrange = float3(1.0f, 0.4f, 0.06f);
static const float3 kFireYellow = float3(1.0f, 0.78f, 0.3f);
static const float3 kFireWhite = float3(1.0f, 0.95f, 0.8f);

// 温度（0〜1）を色にする
float3 FireColor(float temperature) {
	float3 color = lerp(kFireRed, kFireOrange, smoothstep(0.0f, 0.4f, temperature));
	color = lerp(color, kFireYellow, smoothstep(0.4f, 0.75f, temperature));
	return lerp(color, kFireWhite, smoothstep(0.75f, 1.0f, temperature));
}

// 燃え続ける炎の形: 中心から離れるほど薄く、上へ伸びて細く、下は早く薄れる
float FlameShape(float2 uv) {
	float2 offset = uv - kFireShapeCenter;
	offset.x *= 1.0f + kFireTaper * saturate(kFireShapeCenter.y - uv.y);
	offset.y *= offset.y > 0.0f ? kFireStretchDown : kFireStretchUp;
	return saturate(1.0f - length(offset) / kFireShapeRadius);
}

// 火の玉の形: 真ん中ほど濃く、縁へなめらかに薄れる丸
float BallShape(float2 uv) {
	return 1.0f - smoothstep(0.0f, kFireBallRadius, length(uv - 0.5f));
}

// 炎の 1 粒の色（後ろを隠す割合を掛けた色と、後ろを隠す割合）
// 形に空間の模様をかけた濃さを、寿命が進むほど高いしきい値で削り、温度の色で塗る。熱い所ほど明るい
float4 FireParticleColor(float2 uv, float age, float2 noisePos, float fade) {
	float shape = gMaterial.isFireMode == kFireBall ? BallShape(uv) : FlameShape(uv);
	// 模様: 粗い模様と細かい模様を、違う速さで上へ流して混ぜる
	float2 noiseUV = noisePos * kFireNoiseWorldScale + float2(0.0f, gMaterial.fireTime * kFireFlowSpeed);
	float coarse = gTexture.Sample(gSampler, noiseUV).r;
	float detail = gTexture.Sample(gSampler, noiseUV * kFireDetailScale + float2(0.0f, gMaterial.fireTime * kFireFlowSpeed)).r;
	float noise = lerp(coarse, detail, kFireDetailWeight);
	float density = saturate(shape * noise * 2.0f);
	// 削る: 寿命が進むほど、薄い所から透明になる
	float threshold = lerp(kFireErodeStart, 1.0f, age);
	float alpha = smoothstep(threshold, threshold + kFireErodeSoft, density);
	// 温度: 濃い所ほど熱く、寿命につれて急に冷える
	float life = 1.0f - age;
	float temperatureScale = gMaterial.isFireMode == kFireBall ? kFireBallTemperatureScale : kFireTemperatureScale;
	float temperature = saturate(density * life * life * temperatureScale);
	alpha *= saturate(temperature * kFireColdFade) * fade * gMaterial.color.a;
	float intensity = lerp(kFireDimIntensity, kFireHotIntensity, temperature * temperature);
	return float4(FireColor(temperature) * intensity * gMaterial.color.rgb * alpha, alpha * kFireOcclusion);
}

// 速さで伸ばす描き方（火花）
static const float kStreakCoreSharpness = 2.0f;   // 線の真ん中から縁へ暗くなる強さ（大きいほど芯が細い）
static const float kStreakTailBrightness = 0.15f; // 尾の端の明るさ（頭を 1 とする）
static const float kStreakHotIntensity = 4.0f;    // 生まれたときの明るさの倍率（1 を超えた分がブルームでにじむ）
static const float kStreakColdIntensity = 0.6f;   // 寿命の終わりの明るさの倍率

// 速さで伸ばした線の 1 粒の色（足し算で描く。a は色に掛ける割合）
// 頭と尾が丸い棒の形で、真ん中ほど明るく、尾へ行くほど暗い。寿命につれて白 → 黄 → 橙 → 赤へ冷める
float4 StreakParticleColor(float3 streakPos, float age, float fade) {
	// 丸い端の中心どうしを結ぶ線分までの距離（線の半分の太さを 1 とする）
	float segmentHalf = max(streakPos.z - 1.0f, 0.0f);
	float2 toSegment = float2(streakPos.x, streakPos.y - clamp(streakPos.y, -segmentHalf, segmentHalf));
	float core = pow(saturate(1.0f - length(toSegment)), kStreakCoreSharpness);
	// 頭（y が大きい側）から尾へ向かって暗くする
	float fromHead = segmentHalf > 0.0f ? saturate((segmentHalf - streakPos.y) / (2.0f * segmentHalf)) : 0.0f;
	float along = lerp(1.0f, kStreakTailBrightness, fromHead);
	// 温度: 生まれたときが 1、寿命の終わりが 0
	float temperature = 1.0f - age;
	float intensity = lerp(kStreakColdIntensity, kStreakHotIntensity, temperature * temperature);
	float3 color = FireColor(temperature) * intensity * gMaterial.color.rgb;
	return float4(color, core * along * fade * gMaterial.color.a);
}

struct DirectionalLight {
	float4 color;
	float3 direction;
	float intensity;
};

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

PixelShaderOutput main(
VertexShaderOutput input) {
	float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
	PixelShaderOutput output;
	output.color = gMaterial.color * textureColor * input.color;
	
	if (gMaterial.isRingMode != 0) {
		// UV座標の中心からの距離を計算
		float2 uv = input.texcoord - float2(0.5f, 0.5f);
		float dist = length(uv);
		
		// 距離0.4の位置をピークとするリング形状を作成
		float ring = 1.0f - abs(dist - 0.4f) * 20.0f;
		
		 // パーティクルの寿命(アルファ値)に応じてリングの線を細く鋭くする
		float power = lerp(1.0f, 4.0f, input.color.a);
		ring = saturate(ring);
		ring = pow(ring, power);
		
		output.color = gMaterial.color * input.color;
		output.color.a *= ring;
	}
	
	if (gMaterial.isBillboard == kBillboardStreak) {
		output.color = StreakParticleColor(input.streakPos, input.fireAge, input.color.a);
	}

	if (gMaterial.isFireMode != kFireOff) {
		output.color = FireParticleColor(input.texcoord, input.fireAge, input.fireNoisePos, input.color.a);
	}

	// 計算結果がNaNやInfになったピクセルは描画を破棄する
	if (any(isnan(output.color)) || any(isinf(output.color))) {
		discard;
	}
	
	if (output.color.a == 0.0) {
		discard;
	}
	return output;
}

