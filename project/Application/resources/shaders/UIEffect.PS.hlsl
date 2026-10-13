#include "Sprite.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material {
	float4 color;
	float4x4 uvTransform;
};
ConstantBuffer<Material> gMaterial : register(b0);

// 新しく追加するUIエフェクト用の定数バッファ
struct UIEffectParams {
	float time; // 経過時間
	int effectType; // 0: Normal, 1: Wave (波打ち), 2: CloudDissolve (雲の画面遷移)
	float splitY; // エフェクトの基準Y座標
	float amplitude; // 揺れ幅
	float frequency; // 波の細かさ
	float speed; // 波のスピード
	float blurWidth; // 横方向のブラー幅
	float boundaryAmp; // 境界線の波打ち幅
	float progress; // ディゾルブの進行度（しきい値）0〜1
	float edgeWidth; // ディゾルブの縁の光を、覆いきる直前に消していく進行度の幅
	float dissolveFlip; // ディゾルブの縦方向を反転するか（0 or 1）
	float glowStrength; // ディゾルブの縁の光の強さ 0〜1
};
ConstantBuffer<UIEffectParams> gUIEffect : register(b1);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

// 格子のマス座標から 0〜1 の乱数を 2 つ作る（同じマスなら毎回同じ値）。
// シェーダーには rand() がないので、座標を内積で混ぜ、sin の大きな値の小数部を取って
// 「規則性の見えない数」にする。入力が少し変わるだけで結果が大きく変わるのがポイント
float2 Hash22(float2 p) {
	p = float2(dot(p, float2(127.1f, 311.7f)), dot(p, float2(269.5f, 183.3f)));
	return frac(sin(p) * 43758.5453f);
}

// ウォーリーノイズ（セルラーノイズ）：いちばん近い「特徴点」までの距離を返す。
// 空間を 1x1 のマスに区切り、各マスに 1 個ずつランダムな位置に点を置く。
// 最も近い点は自分のマスか周囲 8 マスにしかないので、3x3 だけ調べればよい
float Worley(float2 p) {
	float2 cell = floor(p); // 自分がいるマス
	float2 local = frac(p); // マスの中での位置（0〜1）
	float minDist = 2.0f; // 3x3 の中の点までの距離は必ずこれより小さい
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			float2 offset = float2(x, y);
			// 隣のマスの左上から見た点の位置 = マスのずれ + マス内の乱数位置
			float2 featurePoint = offset + Hash22(cell + offset);
			minDist = min(minDist, length(featurePoint - local));
		}
	}
	return minDist;
}

// 雲のふくらみ：点の真上で 1、離れるほど下がる丸いドーム。
// 1 - d だと頂上が尖ったテントになるので、2 乗して頂上を丸くする。
// ドーム同士の境目は距離の最小値が入れ替わる所なので、鋭い谷（切れ込み）になる
float CloudPuff(float2 p) {
	float d = Worley(p);
	return saturate(1.0f - d * d);
}

// 勾配ノイズ（パーリンノイズ系）：なめらかで柔らかい濃淡を作る。
// マスの 4 隅にランダムな向きの「坂」を置き、坂の高さをなめらかにつないで補間する
float GradientNoise(float2 p) {
	float2 cell = floor(p);
	float2 local = frac(p);
	// 補間の重み。6t^5 - 15t^4 + 10t^3 は端で傾きも曲がり方も 0 になり、マスの境目が見えない
	float2 w = local * local * local * (local * (local * 6.0f - 15.0f) + 10.0f);
	// 各隅の坂（-1〜1 の向き）と、隅から見た今の位置との内積 = その坂での高さ
	float a = dot(Hash22(cell) * 2.0f - 1.0f, local);
	float b = dot(Hash22(cell + float2(1.0f, 0.0f)) * 2.0f - 1.0f, local - float2(1.0f, 0.0f));
	float c = dot(Hash22(cell + float2(0.0f, 1.0f)) * 2.0f - 1.0f, local - float2(0.0f, 1.0f));
	float d = dot(Hash22(cell + float2(1.0f, 1.0f)) * 2.0f - 1.0f, local - float2(1.0f, 1.0f));
	return lerp(lerp(a, b, w.x), lerp(c, d, w.x), w.y) * 0.5f + 0.5f;
}

// fBm の 1 段ごとに強さを何倍にするか。小さいほど細部が弱まり、大きな形が目立つ
static const float kCloudPersistence = 0.35f;

// fBm（フラクタル・ブラウン運動）：大きさ半分・強さ kCloudPersistence 倍のふくらみを何回も重ねる。
// 大きな丸の縁に小さなこぶ、その上にさらに小さなこぶが乗り、輪郭がカリフラワー状になる
float PuffFbm(float2 p) {
	const int kOctaves = 4; // 重ねる回数。増やすほど細部が増えて重くなる
	// 回ごとにずらす量。2 倍しただけだと原点で全段の格子がそろい、模様が重なって見える
	const float2 kOctaveOffset = float2(1.7f, 9.2f);
	float sum = 0.0f;
	float amplitude = 1.0f; // 今の段の強さ
	float totalAmplitude = 0.0f; // 強さの合計（最後に割って 0〜1 に戻す）
	for (int i = 0; i < kOctaves; ++i) {
		sum += CloudPuff(p) * amplitude;
		totalAmplitude += amplitude;
		p = p * 2.0f + kOctaveOffset; // 細かさ 2 倍 = ふくらみの大きさ半分
		amplitude *= kCloudPersistence;
	}
	return sum / totalAmplitude;
}

// 勾配ノイズの fBm：雲の柔らかい濃淡（ふくらみの中のむら）
float SoftFbm(float2 p) {
	const int kOctaves = 5;
	const float2 kOctaveOffset = float2(5.1f, 3.3f);
	float sum = 0.0f;
	float amplitude = 1.0f;
	float totalAmplitude = 0.0f;
	for (int i = 0; i < kOctaves; ++i) {
		sum += GradientNoise(p) * amplitude;
		totalAmplitude += amplitude;
		p = p * 2.0f + kOctaveOffset;
		amplitude *= kCloudPersistence;
	}
	return sum / totalAmplitude;
}

// 雲の 2 層ぶんの生のノイズを返す。x = 奥の柔らかい濃淡, y = 奥のかたまり,
// z = 手前の柔らかい濃淡, w = 手前のかたまり。
// 手前ほど大きく速く流れる（視差）ことで、雲の中を上昇している奥行きを出す
float4 CloudRawNoise(float2 uv, float time) {
	// 画面の縦横比（1280x720）。ノイズが横に引き伸ばされないよう x を広げて読む
	const float kScreenAspect = 16.0f / 9.0f;
	float2 baseUV = uv * float2(kScreenAspect, 1.0f);
	// 画面の縦 1 枚に並ぶ雲のふくらみの数（多いほど小さな雲）
	const float kNearCloudCount = 3.0f;
	const float kFarCloudCount = 6.0f;
	// 1 秒あたりに流れる量（画面の高さ比）。読む位置を上へずらすと模様は下へ動いて見える
	const float kNearScrollSpeed = 0.2f;
	const float kFarScrollSpeed = 0.1f;
	float2 nearUV = (baseUV - float2(0.0f, time * kNearScrollSpeed)) * kNearCloudCount;
	// 奥の層は読む位置をずらして、手前と模様が重ならないようにする
	float2 farUV = (baseUV - float2(0.0f, time * kFarScrollSpeed)) * kFarCloudCount + float2(0.37f, 5.3f);
	return float4(SoftFbm(farUV), PuffFbm(farUV), SoftFbm(nearUV), PuffFbm(nearUV));
}

// 柔らかい濃淡（勾配ノイズ）とかたまり（ウォーリー）を半々に混ぜる
static const float kPuffWeight = 0.5f;

// 2 層を混ぜた雲の濃さ 0〜1。画面のどこから覆うか（前線）を決めるのに使う
float CombinedCloud(float4 raw) {
	// 手前の層を主役にして混ぜる
	const float kNearLayerWeight = 0.6f;
	float soft = lerp(raw.x, raw.z, kNearLayerWeight);
	float puff = lerp(raw.y, raw.w, kNearLayerWeight);
	// 値の 9 割が入る範囲を 0〜1 に引き伸ばす。ウォーリーは頂上が広く谷が細いので高い側に偏る
	const float kSoftMin = 0.43f;
	const float kSoftRange = 0.131f;
	const float kPuffMin = 0.636f;
	const float kPuffRange = 0.269f;
	soft = saturate((soft - kSoftMin) / kSoftRange);
	puff = saturate((puff - kPuffMin) / kPuffRange);
	return lerp(soft, puff, kPuffWeight);
}

// 1 層だけの雲の濃さ 0〜1。混ぜていないぶん値の幅が広いので、引き伸ばす範囲も別に持つ
float LayerCloud(float softRaw, float puffRaw) {
	const float kSoftMin = 0.404f;
	const float kSoftRange = 0.184f;
	const float kPuffMin = 0.557f;
	const float kPuffRange = 0.376f;
	float soft = saturate((softRaw - kSoftMin) / kSoftRange);
	float puff = saturate((puffRaw - kPuffMin) / kPuffRange);
	return lerp(soft, puff, kPuffWeight);
}

// 不透明度：ベールの法則（光は通る物質の量に応じて指数関数的に弱まる）。
// 縁の薄い所は透け、少し内側で急に不透明になる。density = 1 で必ず 1 になるよう割る
float BeerAlpha(float density, float thickness) {
	return (1.0f - exp(-thickness * density)) / (1.0f - exp(-thickness));
}

// 日の当たり具合 0〜1：光（画面の上）の方向に少しずらした所の濃さと比べる。
// 光の側が薄ければ日が当たって明るく、濃ければ自分の影に入って暗い
float SunLight(float noise, float noiseTowardLight) {
	const float kLightContrast = 4.0f; // 濃さの差を明るさの差に何倍で効かせるか
	return saturate(0.5f + (noise - noiseTowardLight) * kLightContrast);
}

PixelShaderOutput main(VertexShaderOutput input) {
    //  UV座標の取得
	float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
	float2 uv = transformedUV.xy;

	if (gUIEffect.effectType == 1) {
		// --- UI波打ちエフェクト ---
		float blurW = (gUIEffect.blurWidth > 0.0f) ? gUIEffect.blurWidth : 0.04f;
		float bAmp = (gUIEffect.boundaryAmp > 0.0f) ? gUIEffect.boundaryAmp : 0.04f;
		float t = gUIEffect.time;
        
		// 波の上端Y座標を計算
		float w1_top = gUIEffect.splitY + 0.02f + sin(uv.x * 2.0f + t * 2.5f) * bAmp * 1.8f;
		float w2_top = gUIEffect.splitY - 0.03f + sin(uv.x * 1.5f - t * 1.8f) * bAmp * 2.0f;
		float base_top = gUIEffect.splitY + sin(uv.x * 1.0f + t * 2.0f) * bAmp * 1.5f;

		// シルエットの境界（最も高い波を採用）
		float silhouette_top = min(min(w1_top, w2_top), base_top);
        
        // シルエット外（波より上）は描画しない
		if (uv.y < silhouette_top) {
			discard;
		}

		// 波の下端Y座標を計算
		float w1_bot = w1_top + 0.12f + sin(uv.x * 3.5f - t * 3.0f) * bAmp * 1.5f;
		float w2_bot = w2_top + 0.20f + sin(uv.x * 2.5f + t * 2.0f) * bAmp * 2.0f;

		// 各波の領域判定
		bool inW1 = (uv.y >= w1_top && uv.y <= w1_bot);
		bool inW2 = (uv.y >= w2_top && uv.y <= w2_bot);

		// ベースカラー設定（波の重なりによる加算）
		// Calculate wave tint color
		float3 waveColor = float3(0.0f, 0.15f, 0.5f);
		if (inW2) { waveColor += float3(0.0f, 0.3f, 0.4f); }
		if (inW1) { waveColor += float3(0.0f, 0.4f, 0.5f); }
        
		float4 waveTintColor = float4(saturate(waveColor), 1.0f);
        
		// Calculate refraction (X-axis distortion)
		float shiftX = cos(uv.x * 1.0f + t * 2.0f) * 1.5f;
		if (inW2) { shiftX += cos(uv.x * 1.5f - t * 1.8f) * 3.0f; }
		if (inW1) { shiftX += cos(uv.x * 2.0f + t * 2.5f) * 3.0f; }
        
		shiftX = shiftX * gUIEffect.amplitude * 0.15f;

		// --- Pixel Rendering ---
		// Apply horizontal motion blur
		float4 blurredColor = float4(0, 0, 0, 0);
		const int samples = 9;
        
		for (int i = 0; i < samples; i++) {
			float2 sampleUV = uv;
			float offset = (i / (float) (samples - 1) - 0.5f) * blurW;
			sampleUV.x += shiftX + offset;
			blurredColor += gTexture.Sample(gSampler, sampleUV);
		}
		blurredColor /= (float) samples;
        
		float4 finalColor;
        
		// 合成処理
		if (blurredColor.a > 0.05f) {
			// テクスチャ範囲内（スクリーン合成）
			float3 baseText = saturate((gMaterial.color * blurredColor).rgb);
			float3 waveColorScreen = saturate(waveTintColor.rgb);
			finalColor.rgb = 1.0f - (1.0f - baseText) * (1.0f - waveColorScreen);
			finalColor.a = blurredColor.a;
		} else {
			// テクスチャ範囲外（背景の半透明波）
			float edgeFade = smoothstep(0.0f, 0.1f, uv.x) * smoothstep(1.0f, 0.9f, uv.x);
			float3 baseBlue = waveTintColor.rgb * 0.8f;
			finalColor = float4(baseBlue, 0.6f * edgeFade);
		}

		PixelShaderOutput output;
		output.color = saturate(finalColor);
		return output;
	}

	if (gUIEffect.effectType == 2) {
		// --- 雲ディゾルブ（画面遷移） ---
		// 雲は「奥の霧」と「手前のかたまり」の 2 層を別々に描いて重ねる。
		// 覆う範囲（前線）は 2 層を混ぜた濃さで決め、しきい値を超えた量を密度にして縁を薄く透けさせる
		const float kLightOffset = 0.025f; // 光の方向へずらす量（画面の高さ比）
		float4 raw = CloudRawNoise(uv, gUIEffect.time);
		float4 rawTowardLight = CloudRawNoise(uv - float2(0.0f, kLightOffset), gUIEffect.time);
		float p = gUIEffect.progress;

		// 縦位置：画面の上ほど 0。晴れるときは反転して、上から晴れるようにする
		float height = lerp(uv.y, 1.0f - uv.y, gUIEffect.dissolveFlip);
		// しきい値と比べる値。ノイズに縦位置を混ぜて、上から順に覆われるようにする。
		// v が小さいほど先に覆われる。ふくらみの中心（noise が高い）から先に覆い、
		// 最後まで残るようにするため 1 - noise を使う
		float noise = CombinedCloud(raw);
		const float kVerticalWeight = 0.75f; // 0 でまだら、1 でまっすぐなカーテン
		float v = lerp(1.0f - noise, height, kVerticalWeight);

		// 密度：しきい値をどれだけ超えたか（0〜1）。kCloudSoftness の幅で 0 から 1 へ上がる。
		// p = 1 で画面全体の密度が 1 に届くよう、しきい値を柔らかさの分だけ先へ進める
		const float kCloudSoftness = 0.2f;
		float threshold = p * (1.0f + kCloudSoftness);
		float density = saturate((threshold - v) / kCloudSoftness);
		const float kCloudThickness = 5.0f; // 大きいほど縁のすぐ内側で不透明になる
		float alpha = BeerAlpha(density, kCloudThickness);

		// 色は画面で見える値（sRGB）で書き、出力先が sRGB なので線形に直して使う
		const float kGamma = 2.2f;
		// 縁の光の色：日差しを受けた温かい白
		const float3 kGlowColorSRGB = float3(1.0f, 0.97f, 0.88f);
		// 雲本体の明るさ。縁がいちばん明るく見えるよう、少し落としておく
		const float kCloudBodyBrightness = 0.9f;
		float3 bodyColorSRGB = gMaterial.color.rgb * kCloudBodyBrightness;
		// 影の色：青空を映した青灰色
		const float3 kCloudShadowColorSRGB = float3(0.74f, 0.80f, 0.91f);
		// 光の向きによる陰影をどれだけ使うか（残りはノイズの濃淡をそのまま明るさにする）
		const float kDirectionalWeight = 0.85f;

		// 奥の霧：空気遠近法。遠くの雲ほど陰影が薄れてかすんで見える
		float farNoise = LayerCloud(raw.x, raw.y);
		float farNoiseTowardLight = LayerCloud(rawTowardLight.x, rawTowardLight.y);
		float farLight = lerp(farNoise, SunLight(farNoise, farNoiseTowardLight), kDirectionalWeight);
		const float kFarHaze = 0.4f; // 陰影をかすみの明るさへ寄せる割合
		const float kHazeLight = 0.6f; // かすみの明るさ
		farLight = lerp(farLight, kHazeLight, kFarHaze);
		float3 farColorSRGB = lerp(kCloudShadowColorSRGB, bodyColorSRGB, farLight);

		// 手前のかたまり：自分の透け・陰影・縁の光を持つ
		const float kClumpThreshold = 0.55f; // 大きいほどかたまりが少ない
		const float kClumpSoftness = 0.15f; // かたまりの縁が透ける幅
		float nearNoise = LayerCloud(raw.z, raw.w);
		float nearNoiseTowardLight = LayerCloud(rawTowardLight.z, rawTowardLight.w);
		float clumpDensity = saturate((nearNoise - kClumpThreshold) / kClumpSoftness);
		float clumpAlpha = BeerAlpha(clumpDensity, kCloudThickness);
		float nearSun = SunLight(nearNoise, nearNoiseTowardLight);
		float3 nearColorSRGB = lerp(kCloudShadowColorSRGB, bodyColorSRGB,
			lerp(nearNoise, nearSun, kDirectionalWeight));
		// かたまりの縁の光：日の当たる側の薄い所を光らせる
		const float kClumpRimStrength = 0.8f;
		float clumpRim = (1.0f - clumpDensity) * step(0.0001f, clumpDensity);
		clumpRim *= smoothstep(0.4f, 0.8f, nearSun) * kClumpRimStrength;
		nearColorSRGB = lerp(nearColorSRGB, kGlowColorSRGB, clumpRim);

		// 手前のかたまりが奥に落とす影：光の側にかたまりがあれば、その下の霧を暗くする
		const float kCastShadowStrength = 0.35f;
		float clumpDensityTowardLight = saturate((nearNoiseTowardLight - kClumpThreshold) / kClumpSoftness);
		float castShadow = BeerAlpha(clumpDensityTowardLight, kCloudThickness) * kCastShadowStrength;
		farColorSRGB = lerp(farColorSRGB, kCloudShadowColorSRGB, castShadow);

		float3 cloudColorSRGB = lerp(farColorSRGB, nearColorSRGB, clumpAlpha);

		// 前線の縁の光：密度の薄い所（日に透ける縁）を光らせる。光るのは日の当たる側だけ
		float frontSun = SunLight(noise, CombinedCloud(rawTowardLight));
		float rim = (1.0f - density) * step(0.0001f, density);
		rim *= smoothstep(0.4f, 0.8f, frontSun);
		// 完全に覆った状態（p = 1）では縁を出さない。そこに近づくほど弱める
		rim *= saturate((1.0f - p) / gUIEffect.edgeWidth);
		rim *= gUIEffect.glowStrength;

		float3 colorSRGB = lerp(cloudColorSRGB, kGlowColorSRGB, rim);
		float3 color = pow(colorSRGB, kGamma);

		PixelShaderOutput output;
		output.color = float4(color, alpha);
		return output;
	}

	// --- 通常描画（エフェクト無効時） ---
	float4 textureColor = gTexture.Sample(gSampler, uv);
	
	if (textureColor.a <= 0.5f) {
		discard;
	}
	
	PixelShaderOutput output;
	output.color = gMaterial.color * textureColor;
	return output;
}
