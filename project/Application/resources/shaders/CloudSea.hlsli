#ifndef CLOUD_SEA_HLSLI
#define CLOUD_SEA_HLSLI

//=========================
// 雲海の値と形の指示
//=========================
static const int kShapeSwell = 0; // 盛り上がり
static const int kShapeTear = 1;  // 裂け目
static const int kMaxShapes = 8;

struct CloudSeaShape {
	float2 center;  // ワールドの XZ
	float radius;   // 盛り上がり: 広がりの半径、裂け目: 穴の最大の半径
	float width;    // 裂け目: 縁のめくれの高さ
	float amount;   // 盛り上がり: 高さ、裂け目: 穴の開き具合（0〜1）
	float crack;    // 盛り上がり: ひびの濃さ
	int kind;
	float padding;
};

struct CloudSeaParams {
	float4x4 viewProjection;
	float3 cameraPosition;
	float time;
	float2 gridOrigin;     // 網の中心（ワールドの XZ）
	float height;          // 雲海の面の高さ
	float amplitude;       // 起伏の高さ（面から上下へ）
	float2 wind;           // 風で模様が流れる速さ（XZ、1 秒あたり）
	float blobSize;        // 雲の塊の大きさ
	float valleyDarkness;  // 谷を暗くする強さ
	float4 color;          // rgb: 雲海の色
	float rimStrength;     // 逆光の縁の強さ
	float rimPower;        // 逆光の縁の鋭さ
	float textureScale;    // 模様の繰り返しの細かさ
	int shapeCount;
	CloudSeaShape shapes[kMaxShapes];
};

ConstantBuffer<CloudSeaParams> gCloudSea : register(b0);

struct CloudSeaVSOutput {
	float4 position : SV_POSITION;
	float3 worldPosition : POSITION0;
	float3 normal : NORMAL0;
	float baseHeight : TEXCOORD0; // 形の指示を入れる前の起伏の高さ（谷の暗さに使う）
};

//=========================
// ノイズ
//=========================

// 2 次元 Dave Hoskins 型ハッシュ（0〜1）
float Hash12(float2 p) {
	float3 p3 = frac(float3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

// 2 次元 Value Noise（3 次の補間。0〜1）
float ValueNoise2D(float2 p) {
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	float n00 = Hash12(i);
	float n10 = Hash12(i + float2(1.0, 0.0));
	float n01 = Hash12(i + float2(0.0, 1.0));
	float n11 = Hash12(i + float2(1.0, 1.0));
	return lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y);
}

static const int kOctaves = 5;
static const float kLacunarity = 2.03;

// FBM（5 回重ねて 0〜1 に正規化）。
// 波長が minWavelength より短い回は平均の値（0.5）に置き換え、網の目より細かい起伏を描かない
float Fbm2D(float2 p, float minWavelength) {
	float sum = 0.0;
	float total = 0.0;
	float amplitude = 0.5;
	float wavelength = 1.0;
	for (int i = 0; i < kOctaves; ++i) {
		// 波長が minWavelength の 2 倍以上で 1、それ以下は 0 へ
		float keep = saturate(wavelength / max(minWavelength, 1e-4) - 1.0);
		sum += amplitude * lerp(0.5, ValueNoise2D(p), keep);
		total += amplitude;
		p = p * kLacunarity + float2(17.1, 31.7);
		amplitude *= 0.5;
		wavelength /= kLacunarity;
	}
	return sum / total;
}

//=========================
// 高さ
//=========================

// 形の指示を入れる前の起伏（面からの高さ、-amplitude 〜 +amplitude）。
// minWavelength はワールドの長さで、これより細かい起伏は描かない
float CloudSeaBaseHeight(float2 xz, float minWavelength) {
	float size = gCloudSea.blobSize;
	float2 p = (xz - gCloudSea.wind * gCloudSea.time) / size;
	float n = Fbm2D(p, minWavelength / size);
	// 0〜1 のノイズを持ち上げて、塊の頭を丸くする
	float b = saturate((n - 0.25) / 0.6);
	return gCloudSea.amplitude * (b * b * (3.0 - 2.0 * b) * 2.0 - 1.0);
}

// 裂け目の穴の縁までの半径（向きごとにノイズで崩す）
float CloudSeaTearEdge(CloudSeaShape s, float2 toPoint) {
	float2 dir = toPoint / max(length(toPoint), 1e-4);
	float n = ValueNoise2D(dir * 2.5 + 40.0) * 0.65 + ValueNoise2D(dir * 6.0 + 7.0) * 0.35;
	return s.radius * s.amount * (0.75 + 0.5 * n);
}

static const float kTearHoleDepth = 8.0; // 裂け目の内側の頂点を沈める深さ

// 形の指示を入れた高さ（面からの高さ）
float CloudSeaApplyShapes(float h, float2 xz) {
	for (int i = 0; i < gCloudSea.shapeCount; ++i) {
		CloudSeaShape s = gCloudSea.shapes[i];
		float2 toPoint = xz - s.center;
		float d = length(toPoint);
		if (s.kind == kShapeSwell) {
			// 中心ほど高い丸い山
			float r = max(s.radius, 1e-3);
			h += s.amount * exp(-(d / r) * (d / r));
		} else if (s.kind == kShapeTear) {
			if (s.amount <= 0.0) {
				continue;
			}
			// 穴の内側は沈め、縁はめくれ上げる
			float edge = CloudSeaTearEdge(s, toPoint);
			float lipWidth = max(s.radius * s.amount * 0.35, 0.3);
			float lip = (d - edge) / lipWidth;
			h = d < edge ? h - kTearHoleDepth : h + s.width * exp(-lip * lip);
		}
	}
	return h;
}

// 形の指示を入れた雲海の面の高さ（ワールドの Y）
float CloudSeaHeight(float2 xz, float minWavelength) {
	return gCloudSea.height + CloudSeaApplyShapes(CloudSeaBaseHeight(xz, minWavelength), xz);
}

#endif
