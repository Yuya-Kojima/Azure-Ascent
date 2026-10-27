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
	float crack;    // 盛り上がり: 泡立ちの強さ
	int kind;
	float padding;
};

// 雲の中の 1 点から照らす光
static const int kMaxGlows = 4;

struct CloudSeaGlow {
	float3 position; // ワールドの位置
	float radius;    // 光が届く広がり（この距離で 1/e に弱まる）
	float3 color;    // 光の色と強さ
	float padding;
};

struct CloudSeaParams {
	float4x4 inverseViewProjection;
	float3 cameraPosition;
	float time;
	float height;          // 雲の層の上の面の基準の高さ
	float amplitude;       // 上の面の起伏の高さ（基準から上下へ）
	float blobSize;        // 上の面の塊の大きさ
	float thickness;       // 層の厚み（基準の高さから下へ）
	float2 wind;           // 風で雲が流れる速さ（XZ、1 秒あたり）
	float edgeSoftness;    // 上の面から濃さが 1 になるまでの深さ
	float density;         // 濃さ 1 の所で、長さ 1 あたりに光が弱まる割合
	float4 color;          // rgb: 雲の色（光を受けて返す割合）
	float brightness;      // 雲が返す光の強さの倍率
	float powder;          // 粉の暗さの強さ（0〜1）
	float valleyDarkness;  // 低い所の環境光を暗くする強さ
	float forwardScatter;  // 光の進む向きへ強く散る度合いの混ぜ具合（0〜1）
	float rimStrength;     // 地平線の光が雲の上の縁を透ける強さ
	float rimPower;        // 地平線の光の縁の鋭さ
	float noiseSize;       // 大きなノイズ 1 枚のワールドの長さ
	float detailSize;      // 細かいノイズ 1 枚のワールドの長さ
	float detailStrength;  // 細かいノイズで縁を削る強さ
	float jitter;          // 視線の出発点を画素ごとにずらす量（0〜1）
	int debugView;         // 調べるための表示（0: 通常, 1: 太陽の光だけ, 2: 環境光だけ, 3: 濃さだけ, 4: 濃さを測った回数）
	int shapeCount;
	float2 screenSize;     // 元の画面の大きさ（画素）
	float projection22;    // 投影行列の [2][2]（深度から奥行きを戻すのに使う）
	float projection32;    // 投影行列の [3][2]
	CloudSeaShape shapes[kMaxShapes];
	CloudSeaGlow glows[kMaxGlows];
	int glowCount;
};

ConstantBuffer<CloudSeaParams> gCloudSea : register(b0);

Texture3D<float4> gBaseNoise : register(t1);
Texture3D<float4> gDetailNoise : register(t2);
// 上の面の起伏（0〜1、繰り返して貼る。1 枚に塊が kTopNoiseCells × kTopNoiseCells 個）
Texture2D<float> gTopNoise : register(t3);
SamplerState gNoiseSampler : register(s0);

// 上の面の起伏のノイズ 1 枚に並ぶ一番大きな塊の数（一辺）。CloudTopNoise.CS.hlsl と同じ
static const float kTopNoiseCells = 16.0;

float Remap(float value, float low, float high, float newLow, float newHigh) {
	return newLow + (value - low) / (high - low) * (newHigh - newLow);
}

//=========================
// 上の面の高さ
//=========================

// 形の指示を入れる前の起伏（基準からの高さ、-amplitude 〜 +amplitude）
float CloudSeaBaseHeight(float2 xz) {
	float size = gCloudSea.blobSize;
	float2 p = (xz - gCloudSea.wind * gCloudSea.time) / size;
	float n = gTopNoise.SampleLevel(gNoiseSampler, p / kTopNoiseCells, 0);
	// 0〜1 のノイズを持ち上げて、塊の頭を丸くする
	float b = saturate((n - 0.25) / 0.6);
	return gCloudSea.amplitude * (b * b * (3.0 - 2.0 * b) * 2.0 - 1.0);
}

// 形の指示を入れた上の面の高さ（ワールドの Y）
float CloudSeaTopHeight(float2 xz) {
	float h = CloudSeaBaseHeight(xz);
	for (int i = 0; i < gCloudSea.shapeCount; ++i) {
		CloudSeaShape s = gCloudSea.shapes[i];
		if (s.kind == kShapeSwell) {
			// 中心ほど高い丸い山
			float d = length(xz - s.center) / max(s.radius, 1e-3);
			h += s.amount * exp(-d * d);
		}
	}
	return gCloudSea.height + h;
}

// 上の面の一番高い所になりうる高さ（ワールドの Y）
float CloudSeaMaxTopHeight() {
	float swell = 0.0;
	for (int i = 0; i < gCloudSea.shapeCount; ++i) {
		CloudSeaShape s = gCloudSea.shapes[i];
		if (s.kind == kShapeSwell) {
			swell = max(swell, s.amount);
		}
	}
	return gCloudSea.height + gCloudSea.amplitude + swell;
}

// 層の底の高さ（ワールドの Y）
float CloudSeaBottomHeight() {
	return gCloudSea.height - gCloudSea.thickness;
}

//=========================
// 濃さ
//=========================

// 層の底で濃さを 0 へ下げていく幅
static const float kBottomFade = 6.0;
// 細かいノイズが風とは別に上へ流れる速さ（1 秒あたり。表面の形がゆっくり変わる）
static const float kDetailRiseSpeed = 0.25;

// p での雲の濃さ（0〜1）。top はその真上の上の面の高さ。
// useDetail が false のときは細かいノイズで削らない（光の向きへ測るときの軽い版）
float CloudSeaDensity(float3 p, float top, bool useDetail) {
	float depthBelow = top - p.y;
	if (depthBelow <= 0.0) {
		return 0.0;
	}
	// 高さによる形：上の面のすぐ下は薄く、edgeSoftness の深さで 1。底へ向けて薄く
	float shape = saturate(depthBelow / max(gCloudSea.edgeSoftness, 1e-3));
	shape *= saturate((p.y - CloudSeaBottomHeight()) / kBottomFade);
	if (shape <= 0.0) {
		return 0.0;
	}

	// 大きなノイズ：Perlin-Worley を Worley の重ねで削った塊
	float3 windOffset = float3(gCloudSea.wind.x, 0.0, gCloudSea.wind.y) * gCloudSea.time;
	float4 baseNoise = gBaseNoise.SampleLevel(gNoiseSampler, (p - windOffset) / gCloudSea.noiseSize, 0);
	float worleyFbm = dot(baseNoise.gba, float3(0.625, 0.25, 0.125));
	float baseCloud = saturate(Remap(baseNoise.r, worleyFbm - 1.0, 1.0, 0.0, 1.0));

	// 上の面に近い所（shape が小さい所）ほど、ノイズの高い所だけを残す
	float density = saturate(Remap(baseCloud, 1.0 - shape, 1.0, 0.0, 1.0));
	if (!useDetail || density <= 0.0) {
		return density;
	}

	// 細かいノイズで、濃さの低い縁ほど強く削る
	float3 detailOffset = windOffset + float3(0.0, kDetailRiseSpeed * gCloudSea.time, 0.0);
	float3 detailNoise = gDetailNoise.SampleLevel(gNoiseSampler, (p - detailOffset) / gCloudSea.detailSize, 0).rgb;
	float detailFbm = dot(detailNoise, float3(0.625, 0.25, 0.125));
	return saturate(Remap(density, detailFbm * gCloudSea.detailStrength, 1.0, 0.0, 1.0));
}

#endif
