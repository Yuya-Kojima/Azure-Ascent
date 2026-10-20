#include "Fullscreen.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
Texture2D<float32_t> gDepthTexture : register(t1);
Texture2D<float32_t> gMaskTexture : register(t2);
Texture2D<float32_t4> gBloomTexture : register(t3);
Texture2D<float32_t4> gHistoryTexture : register(t4);
SamplerState gSampler : register(s0);
SamplerState gSamplerPoint : register(s1);

cbuffer PostProcessData : register(b0) {
	int32_t postEffectType;
	int32_t useDepthOutline;
	int32_t useRadialBlur;
	int32_t useDissolve;
	int32_t useGaussianFilter;
	int32_t useGrayscale;
	int32_t useVignette;
	int32_t boxFilterK;
	float32_t3 monotoneColor;
	float32_t vignetteScale;
	float32_t vignetteExponent;
	int32_t gaussianFilterK;
	float32_t gaussianSigma;
	float32_t depthOutlineWeight;
	float32_t depthOutlineAttenuation;
	float32_t3 flashColor;
	float32_t4x4 projectionInverse;
	float32_t2 radialBlurCenter;
	float32_t radialBlurWidth;
	int32_t radialBlurSamples;
	float32_t radialBlurInnerRadius;
	float32_t radialBlurOuterRadius;
	float32_t radialBlurAberration;
	float32_t padding4;
	float32_t dissolveThreshold;
	float32_t dissolveEdgeRange;
	float32_t2 padding5;
	float32_t3 dissolveEdgeColor;
	float32_t time;
	float32_t hsvFilterHue;
	float32_t hsvFilterSaturation;
	float32_t hsvFilterValue;
	float32_t padding6;
	float32_t bloomIntensity;
	int32_t useBloom;
	int32_t toneMappingType;
	float32_t exposure;
	float32_t motionBlurAlpha;
	float32_t dofFocusDistance;
	float32_t dofFocusRange;
	float32_t flashIntensity;
	int32_t activeShockwaveCount;
	float32_t3 padding9;
	struct ShockwaveData {
		float32_t weight;
		float32_t distortion;
		float32_t radius;
		float32_t thickness;
		float32_t2 center;
		float32_t aberration;
		float32_t padding;
	} shockwaves[5];
};

static const float32_t PI = 3.14159265f;

// ガウシアンぼかしの重み計算
// 指定された距離に応じた「ぼかしの強さ（重み）」を計算する関数
// ガウシアンフィルターや被写界深度（DoF）で使用
float32_t gauss(float32_t x, float32_t y, float32_t sigma) {
	float32_t exponent = -(x * x + y * y) * rcp(2.0f * sigma * sigma);
	float32_t denominator = 2.0f * PI * sigma * sigma;
	return exp(exponent) * rcp(denominator);
}

//ノイズ用ランダム値生成
//砂嵐で使用
float32_t rand2dTo1d(float32_t2 value) {
    // 疑似乱数生成の標準的なアルゴリズム
	return frac(sin(dot(value, float32_t2(12.9898f, 78.233f))) * 43758.5453f);
}

//明るさ（輝度）変換
//RGBの色を抜いて、白黒の明るさに変換する
//アウトラインで使用
float32_t Luminance(float32_t3 v) {
	return dot(v, float32_t3(0.2125f, 0.7154f, 0.0721f));
}

static const float32_t kPrewittHorizontalKernel[3][3] = {
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
	{ -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
};

static const float32_t kPrewittVerticalKernel[3][3] = {
	{ -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
	{ 0.0f, 0.0f, 0.0f },
	{ 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f },
};

//数値の範囲内ループ処理
//数値が最大値を超えたら最小値に戻る
//HSVフィルターで使用
float32_t WrapValue(float32_t value, float32_t minRange, float32_t maxRange) {
	float32_t range = maxRange - minRange;
	float32_t modValue = fmod(value - minRange, range);
	if (modValue < 0.0f) {
		modValue += range;
	}
	return minRange + modValue;
}

//RGBからHSVに変換
float32_t3 RGBToHSV(float32_t3 rgb) {
	float32_t r = rgb.r;
	float32_t g = rgb.g;
	float32_t b = rgb.b;
	float32_t maxColor = max(r, max(g, b));
	float32_t minColor = min(r, min(g, b));
	float32_t delta = maxColor - minColor;
    
	float32_t h = 0.0f;
	float32_t s = 0.0f;
	float32_t v = maxColor;
    
	if (maxColor > 0.0f) {
		s = delta / maxColor;
	}
    
	if (delta > 0.0f) {
		if (maxColor == r) {
			h = (g - b) / delta;
		} else if (maxColor == g) {
			h = 2.0f + (b - r) / delta;
		} else if (maxColor == b) {
			h = 4.0f + (r - g) / delta;
		}
		h /= 6.0f;
		if (h < 0.0f) {
			h += 1.0f;
		}
	}
    
	return float32_t3(h, s, v);
}

//HSVからRGB	
float32_t3 HSVToRGB(float32_t3 hsv) {
	float32_t h = hsv.x;
	float32_t s = hsv.y;
	float32_t v = hsv.z;
    
	float32_t r = 0.0f, g = 0.0f, b = 0.0f;
    
	if (s == 0.0f) {
		r = v;
		g = v;
		b = v;
	} else {
		h = fmod(h, 1.0f);
		if (h < 0.0f)
			h += 1.0f;
		h *= 6.0f;
		int32_t i = (int32_t) floor(h);
		float32_t f = h - (float32_t) i;
		float32_t p = v * (1.0f - s);
		float32_t q = v * (1.0f - s * f);
		float32_t t = v * (1.0f - s * (1.0f - f));
        
		if (i == 0) {
			r = v;
			g = t;
			b = p;
		} else if (i == 1) {
			r = q;
			g = v;
			b = p;
		} else if (i == 2) {
			r = p;
			g = v;
			b = t;
		} else if (i == 3) {
			r = p;
			g = q;
			b = v;
		} else if (i == 4) {
			r = t;
			g = p;
			b = v;
		} else {
			r = v;
			g = p;
			b = q;
		}
	}
    
	return float32_t3(r, g, b);
}

//トーンマッピング
//画面のまぶしすぎる白飛びを抑え、映画のフィルムのような立体感のある色合いにする
float32_t3 ToneMap_ACES(float32_t3 x) {
	float a = 2.51f;
	float b = 0.03f;
	float c = 2.43f;
	float d = 0.59f;
	float e = 0.14f;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

//Rinehardトーンマッピング
//画面のまぶしすぎる白飛びを抑え、マイルドな明るさに抑える
float32_t3 ToneMap_Reinhard(float32_t3 x) {
	return x / (1.0f + x);
}
// ==========================================

// 衝撃波で読む位置をずらす量を求める
// 戻り値は緑のずらし量。赤・青は aberrationOffset だけ足し引きする
// 半径・太さ・押し曲げる量は画面の高さを 1 とした長さ
float32_t2 ComputeShockwaveOffset(float32_t2 texcoord, out float32_t2 aberrationOffset) {
	float32_t2 offset = float32_t2(0.0f, 0.0f);
	aberrationOffset = float32_t2(0.0f, 0.0f);

	uint32_t width, height;
	gTexture.GetDimensions(width, height);
	float32_t aspect = (float32_t) width / (float32_t) height;

	for (int32_t i = 0; i < activeShockwaveCount; ++i) {
		float32_t2 dir = texcoord - shockwaves[i].center;
		float32_t dist = length(float32_t2(dir.x * aspect, dir.y));

		if (dist > 0.0001f) {
			float32_t diff = abs(dist - shockwaves[i].radius);

			if (diff < shockwaves[i].thickness) {
				// 輪の真ん中で最大、縁で 0 になる山の形で押し曲げる
				float32_t force = 1.0f - (diff / shockwaves[i].thickness);
				float32_t distortion = sin(force * PI) * shockwaves[i].distortion * shockwaves[i].weight;
				float32_t2 push = (dir / dist) * distortion;
				offset += push;
				aberrationOffset += push * shockwaves[i].aberration;
			}
		}
	}
	return offset;
}

struct PixelShaderOutput {
	float32_t4 color : SV_TARGET0;
	float32_t4 color1 : SV_TARGET1;
};

PixelShaderOutput main(VertexShaderOutput input) {
	PixelShaderOutput output;

	// 衝撃波で読む位置をずらし、以降の段はすべてずらした位置で読む
	float32_t2 uv = input.texcoord;
	float32_t2 uvR = input.texcoord;
	float32_t2 uvB = input.texcoord;
	if (activeShockwaveCount > 0) {
		float32_t2 aberrationOffset;
		uv += ComputeShockwaveOffset(input.texcoord, aberrationOffset);
		uvR = uv + aberrationOffset;
		uvB = uv - aberrationOffset;
	}

	output.color = gTexture.Sample(gSampler, uv);
	if (activeShockwaveCount > 0) {
		// 赤は少し多く、青は少し少なくずらして輪の縁に色ずれを出す
		output.color.r = gTexture.Sample(gSampler, uvR).r;
		output.color.b = gTexture.Sample(gSampler, uvB).b;
	}
    
	if (postEffectType == 1) { // BoxFilter
		uint32_t width, height;
		gTexture.GetDimensions(width, height);
		float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));
        
		output.color.rgb = float32_t3(0.0f, 0.0f, 0.0f);
		output.color.a = 1.0f;

		float32_t kSize = (float32_t) boxFilterK;
		float32_t weight = 1.0f / ((2.0f * kSize + 1.0f) * (2.0f * kSize + 1.0f));

		for (int32_t x = -boxFilterK; x <= boxFilterK; ++x) {
			for (int32_t y = -boxFilterK; y <= boxFilterK; ++y) {
				float32_t2 texcoord = uv + float32_t2(x, y) * uvStepSize;
				float32_t3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                
                // NaNやInfが含まれている場合は0に置き換えて、周りへブロック状に広がるのを防ぐ
				if (any(isnan(fetchColor)) || any(isinf(fetchColor))) {
					fetchColor = float32_t3(0.0f, 0.0f, 0.0f);
				}
                
				output.color.rgb += fetchColor * weight;
			}
		}
	}
	if (useGaussianFilter != 0) { // GaussianFilter
		uint32_t width, height;
		gTexture.GetDimensions(width, height);
		float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));
        
		output.color.rgb = float32_t3(0.0f, 0.0f, 0.0f);
		output.color.a = 1.0f;

		float32_t totalWeight = 0.0f;
		for (int32_t x = -gaussianFilterK; x <= gaussianFilterK; ++x) {
			for (int32_t y = -gaussianFilterK; y <= gaussianFilterK; ++y) {
				float32_t weight = gauss((float32_t) x, (float32_t) y, gaussianSigma);
				float32_t2 texcoord = uv + float32_t2(x, y) * uvStepSize;
				float32_t3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                
                // NaNやInfが含まれている場合は0に置き換えて、周りへブロック状に広がるのを防ぐ
				if (any(isnan(fetchColor)) || any(isinf(fetchColor))) {
					fetchColor = float32_t3(0.0f, 0.0f, 0.0f);
				}
                
				output.color.rgb += fetchColor * weight;
				totalWeight += weight;
			}
		}
        
		output.color.rgb *= rcp(totalWeight);
	}
	if (postEffectType == 3) { // Luminance Outline
		uint32_t width, height;
		gTexture.GetDimensions(width, height);
		float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));
        
		float32_t2 difference = float32_t2(0.0f, 0.0f);
        
		for (int32_t x = 0; x < 3; ++x) {
			for (int32_t y = 0; y < 3; ++y) {
				float32_t2 texcoord = uv + float32_t2(x - 1, y - 1) * uvStepSize;
				float32_t3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
				float32_t luminance = Luminance(fetchColor);
                
				difference.x += luminance * kPrewittHorizontalKernel[x][y];
				difference.y += luminance * kPrewittVerticalKernel[x][y];
			}
		}
        
		float32_t weight = length(difference);
        // 差が小さすぎて分かりづらいので6倍して強調
		weight = saturate(weight * 6.0f);
        
        // 元の色を取得し、エッジ（weight）が強いほど黒く（0に近づく）するように合成
		float32_t3 originalColor = gTexture.Sample(gSampler, uv).rgb;
		output.color.rgb = (1.0f - weight) * originalColor;
		output.color.a = 1.0f;
	}
	if (useDepthOutline != 0) { // Depth Outline
		uint32_t width, height;
		gTexture.GetDimensions(width, height);
		float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));
        
		float32_t2 difference = float32_t2(0.0f, 0.0f);
		float32_t centerViewZ = 0.0f;
        
		for (int32_t x = 0; x < 3; ++x) {
			for (int32_t y = 0; y < 3; ++y) {
				float32_t2 texcoord = uv + float32_t2(x - 1, y - 1) * uvStepSize;
                
                // NDCのDepthを取得
				float32_t ndcDepth = gDepthTexture.Sample(gSamplerPoint, texcoord);
                
                // View空間に戻す
				float32_t4 viewSpace = mul(float32_t4(0.0f, 0.0f, ndcDepth, 1.0f), projectionInverse);
				float32_t viewZ = viewSpace.z * rcp(viewSpace.w);
                
				if (x == 1 && y == 1) {
					centerViewZ = viewZ;
				}
                
				difference.x += viewZ * kPrewittHorizontalKernel[x][y];
				difference.y += viewZ * kPrewittVerticalKernel[x][y];
			}
		}
        
		float32_t weight = length(difference) * depthOutlineWeight;
        // 距離による減衰（絶対値にして負数エラーを防ぐ）
		weight = weight / (1.0f + abs(centerViewZ) * depthOutlineAttenuation);
		weight = saturate(weight);
        
		output.color.rgb = (1.0f - weight) * output.color.rgb;
	}
	if (useRadialBlur != 0) { // Radial Blur
        // 中心から現在のUVへの方向と距離を計算
		float32_t2 direction = uv - radialBlurCenter;
		float32_t dist = length(direction);
        
        // 中心マスク（センターをクッキリさせる）
        // innerRadius 以下はブラー0、outerRadius 以上はブラー100%
		float32_t mask = smoothstep(radialBlurInnerRadius, radialBlurOuterRadius, dist);
        
        // maskが0より大きい場合のみ処理を行う
		if (mask > 0.0f) {
			float32_t3 outputColor = float32_t3(0.0f, 0.0f, 0.0f);
            
			for (int32_t sampleIndex = 0; sampleIndex < radialBlurSamples; ++sampleIndex) {
                // 色収差のオフセット計算
				float32_t scale = radialBlurWidth * float32_t(sampleIndex);
                
				float32_t2 texcoordR = uvR + direction * (scale * (1.0f + radialBlurAberration));
				float32_t2 texcoordG = uv + direction * scale;
				float32_t2 texcoordB = uvB + direction * (scale * (1.0f - radialBlurAberration));
                
				outputColor.r += gTexture.Sample(gSampler, texcoordR).r;
				outputColor.g += gTexture.Sample(gSampler, texcoordG).g;
				outputColor.b += gTexture.Sample(gSampler, texcoordB).b;
			}
            
            // 平均化する
			outputColor.rgb *= rcp(float32_t(radialBlurSamples));
            
            // 現在の色とブラー画像をマスク値でブレンドする
			output.color.rgb = lerp(output.color.rgb, outputColor, mask);
		}
	}
	if (useDissolve != 0) { // Dissolve
		float32_t mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
        
		if (mask <= dissolveThreshold) {
			discard;
		}
        
		float32_t edge = 1.0f - smoothstep(dissolveThreshold, dissolveThreshold + dissolveEdgeRange, mask);
        
		output.color.rgb = output.color.rgb + edge * dissolveEdgeColor;
	}
	if (postEffectType == 7) { // Depth of Field (DoF)
		uint32_t width, height;
		gTexture.GetDimensions(width, height);
		float32_t2 uvStepSize = float32_t2(rcp(width), rcp(height));
        
        // 現在のピクセルのNDC Depthを取得
		float32_t ndcDepth = gDepthTexture.Sample(gSamplerPoint, uv);
        
        // DepthをView空間に変換 (Z値をリニアにする)
		float32_t4 viewSpace = mul(float32_t4(0.0f, 0.0f, ndcDepth, 1.0f), projectionInverse);
		float32_t viewZ = viewSpace.z * rcp(viewSpace.w);
        
        // CoC (Circle of Confusion / 錯乱円) の計算
        // ピント位置からどれくらい離れているか
		float32_t blurAmount = abs(viewZ - dofFocusDistance) - dofFocusRange;
        // ブラーの強さを0.0 ～ 1.0にクランプ (除数はブラーの強さ係数。小さいほど急激にボケる)
		float32_t coc = saturate(blurAmount / 15.0f);
        
		if (coc <= 0.0f) {
			output.color.rgb = gTexture.Sample(gSampler, uv).rgb;
		} else {
            // シンプルなガウスぼかし（CoCに応じてぼかし幅を変える）
			float32_t3 blurColor = float32_t3(0.0f, 0.0f, 0.0f);
			float32_t totalWeight = 0.0f;
            
            // 最大カーネルサイズ (coc = 1.0のとき最大値)
			int32_t kSize = (int32_t) ceil(coc * 4.0f);
			if (kSize < 1)
				kSize = 1;
            
			for (int32_t x = -kSize; x <= kSize; ++x) {
				for (int32_t y = -kSize; y <= kSize; ++y) {
					float32_t2 texcoord = uv + float32_t2(x, y) * uvStepSize;
					float32_t3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
                    
                    // NaN/Infサニタイズ
					if (any(isnan(fetchColor)) || any(isinf(fetchColor))) {
						fetchColor = float32_t3(0.0f, 0.0f, 0.0f);
					}
                    
					blurColor += fetchColor;
					totalWeight += 1.0f;
				}
			}
			blurColor *= rcp(totalWeight);
            
            // 元のピクセルとボケたピクセルをCoCでブレンド
			output.color.rgb = lerp(gTexture.Sample(gSampler, uv).rgb, blurColor, coc);
		}
		output.color.a = 1.0f;
	}
	if (postEffectType == 8) { // Random Noise
        // 経過時間を掛けてSeed値にすることで、毎フレーム異なる乱数を生成する
		float32_t random = rand2dTo1d(input.texcoord * time);
        
        // 元の色を取得
		float32_t3 originalColor = gTexture.Sample(gSampler, uv).rgb;
        
        // 生成した乱数の値を入力画像に乗算して出力（レトロなノイズ感）
		output.color.rgb = originalColor * random;
		output.color.a = 1.0f;
	}
	if (postEffectType == 9) { // HSV Filter
		float32_t4 textureColor = gTexture.Sample(gSampler, uv);
		float32_t3 hsv = RGBToHSV(textureColor.rgb);
        
        // パラメータを増減させる
		hsv.x += hsvFilterHue;
		hsv.y += hsvFilterSaturation;
		hsv.z += hsvFilterValue;
        
        // 色相は角度なのでぐるぐる回るようにする
		hsv.x = WrapValue(hsv.x, 0.0f, 1.0f);
		hsv.y = saturate(hsv.y);
		hsv.z = saturate(hsv.z);
        
		float32_t3 rgb = HSVToRGB(hsv);
        
		output.color.rgb = rgb;
		output.color.a = textureColor.a;
	}
	if (useGrayscale != 0) {
		float32_t value = dot(output.color.rgb, float32_t3(0.2125f, 0.7154f, 0.0721f));
		output.color.rgb = value * monotoneColor;
	}

	if (useVignette != 0) {
        // 周囲を0に、中心になるほど明るくなるように計算で調整
		float32_t2 correct = input.texcoord * (1.0f - input.texcoord.yx);
        // correctだけで計算すると中心の最大値が0.0625で暗すぎるのでScaleで調整。この例では16倍して1にしている
		float vignette = correct.x * correct.y * vignetteScale;
        // とりあえず0.8乗でそれっぽくしてみた
		vignette = saturate(pow(vignette, vignetteExponent));
        // 係数として乗算
		output.color.rgb *= vignette;
	}

	if (useBloom != 0) {
		float32_t4 bloomColor = gBloomTexture.Sample(gSampler, uv);
		output.color.rgb += bloomColor.rgb * bloomIntensity;
	}

    // --- モーションブラー（残像エフェクト）---
	float32_t3 finalHDRColor = output.color.rgb;
    
    // NaN汚染対策 (モデルの法線エラー等で描画結果にNaNが含まれる場合、NaNが伝播して黒い残像が永久に残るのを防ぐ)
	if (any(isnan(finalHDRColor)) || any(isinf(finalHDRColor))) {
		finalHDRColor = float32_t3(0.0f, 0.0f, 0.0f);
	}

	if (motionBlurAlpha > 0.0f) {
		float32_t4 historyColor = gHistoryTexture.Sample(gSampler, input.texcoord);
		if (any(isnan(historyColor.rgb)) || any(isinf(historyColor.rgb))) {
			historyColor.rgb = float32_t3(0.0f, 0.0f, 0.0f);
		}
		finalHDRColor = lerp(finalHDRColor, historyColor.rgb, motionBlurAlpha);
	}
    
    // 次フレームのためのHDRカラーをSV_TARGET1 (historyTexture_) に保存
	output.color1 = float32_t4(finalHDRColor, 1.0f);

    // --- Tone Mapping ---
    // 露出(Exposure)を適用
	finalHDRColor *= exposure;

	if (toneMappingType == 1) {
        // ACES Filmic
		finalHDRColor = ToneMap_ACES(finalHDRColor);
	} else if (toneMappingType == 2) {
        // Reinhard
		finalHDRColor = ToneMap_Reinhard(finalHDRColor);
	} else {
        // トーンマッピングなし
		finalHDRColor = saturate(finalHDRColor);
	}

	output.color = float32_t4(finalHDRColor, 1.0f);

	// 画面フラッシュ（汎用カラー）の適用
	output.color.rgb = lerp(output.color.rgb, flashColor, flashIntensity);

	return output;
}
