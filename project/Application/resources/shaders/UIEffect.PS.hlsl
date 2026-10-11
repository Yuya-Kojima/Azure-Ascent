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
	float edgeWidth; // ディゾルブの縁を光らせる幅
	float dissolveFlip; // ディゾルブの縦方向を反転するか（0 or 1）
	float glowStrength; // ディゾルブの縁の光の強さ 0〜1
};
ConstantBuffer<UIEffectParams> gUIEffect : register(b1);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

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
		// 雲は大きさと流れる速さの違う 2 層を重ねる。
		// 手前ほど大きく速く流れる（視差）ことで、雲の中を上昇している奥行きを出す
		// 画面の縦横比（1280x720）。ノイズが横に引き伸ばされないよう x を広げて読む
		const float kScreenAspect = 16.0f / 9.0f;
		float2 baseUV = uv * float2(kScreenAspect, 1.0f);
		// 雲の大きさ（ノイズを何倍に拡大して読むか。小さいほど大きな雲）
		const float kNearCloudScale = 0.45f;
		const float kFarCloudScale = 0.9f;
		// 1秒あたりに流れる量（テクスチャ比）。サンプル位置を上へずらすと模様は下へ動いて見える
		const float kNearScrollSpeed = 0.22f;
		const float kFarScrollSpeed = 0.08f;
		float2 nearUV = baseUV * kNearCloudScale;
		nearUV.y -= gUIEffect.time * kNearScrollSpeed;
		// 奥の層は x もずらして、手前と模様が重ならないようにする
		float2 farUV = baseUV * kFarCloudScale + float2(0.37f, 0.0f);
		farUV.y -= gUIEffect.time * kFarScrollSpeed;

		// テクスチャはグレースケールのノイズ。r だけ使えば十分
		float nearNoise = gTexture.Sample(gSampler, nearUV).r;
		float farNoise = gTexture.Sample(gSampler, farUV).r;
		// 手前の層を主役にして混ぜる
		const float kNearLayerWeight = 0.6f;
		float noise = lerp(farNoise, nearNoise, kNearLayerWeight);
		// noise0.png の値は 0.36〜0.64 に 9 割が入る（実測）。2 層を混ぜると平均化で
		// 幅がさらに約 0.7 倍に縮むので、中央 0.5 の前後 0.145 を 0〜1 に引き伸ばす
		const float kNoiseMin = 0.355f;
		const float kNoiseRange = 0.29f;
		noise = saturate((noise - kNoiseMin) / kNoiseRange);

		float p = gUIEffect.progress;
		float edge = gUIEffect.edgeWidth;

		// 縦位置：画面の上ほど 0。晴れるときは反転して、上から晴れるようにする
		float height = lerp(uv.y, 1.0f - uv.y, gUIEffect.dissolveFlip);
		// しきい値と比べる値。ノイズに縦位置を混ぜて、上から順に覆われるようにする
		const float kVerticalWeight = 0.75f; // 0 でまだら、1 でまっすぐなカーテン
		float v = lerp(noise, height, kVerticalWeight);

		// 覆うかどうか：v <= p で不透明。境目は kCloudSoftness の幅でなめらかに透ける
		const float kCloudSoftness = 0.03f;
		float cover = 1.0f - smoothstep(p, p + kCloudSoftness, v);

		// 縁の光：雲の内側の輪郭（p - edge 〜 p）を明るくする（日に照らされた雲の縁）。
		// 透明な側に半透明の白を描くと、背景と混ざって灰色にくすむため内側に描く
		float rim = smoothstep(p - edge, p, v) * cover;
		// 完全に覆った状態（p = 1）では縁を出さない。そこに近づくほど弱める
		rim *= saturate((1.0f - p) / edge);
		rim *= gUIEffect.glowStrength;

		// 色は画面で見える値（sRGB）で書き、出力先が sRGB なので線形に直して使う
		const float kGamma = 2.2f;
		// 縁の光の色：日差しを受けた温かい白
		const float3 kGlowColorSRGB = float3(1.0f, 0.97f, 0.88f);
		// 雲本体の明るさ。縁がいちばん明るく見えるよう、少し落としておく
		const float kCloudBodyBrightness = 0.9f;
		// 影の色：青空を映した青灰色
		const float3 kCloudShadowColorSRGB = float3(0.74f, 0.80f, 0.91f);

		// ノイズの濃淡を雲の陰影に使う（暗い所は影、明るい所は白）
		float3 cloudColorSRGB = lerp(kCloudShadowColorSRGB,
			gMaterial.color.rgb * kCloudBodyBrightness, noise);
		// 縁は光の色へ寄せる
		float3 colorSRGB = lerp(cloudColorSRGB, kGlowColorSRGB, rim);
		float3 color = pow(colorSRGB, kGamma);
		float alpha = cover;

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
