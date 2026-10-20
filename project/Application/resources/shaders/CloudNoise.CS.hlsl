//=========================
// 雲の 3 次元ノイズを作る（端どうしがつながる＝繰り返して貼れる）
// 大きなノイズ: r = Perlin-Worley、g・b・a = 細かさを変えた Worley の重ね
// 細かいノイズ: r・g = 細かさを変えた Worley の重ね、b = 一番細かい Worley
//=========================

struct NoiseParams {
	uint size; // テクスチャの一辺の画素数
	uint kind; // 0: 大きなノイズ, 1: 細かいノイズ
};

ConstantBuffer<NoiseParams> gParams : register(b0);
RWTexture3D<float4> gOutput : register(u0);

static const uint kKindBase = 0;

// 整数の 3 次元ハッシュ（pcg3d）
uint3 Hash33(uint3 v) {
	v = v * 1664525u + 1013904223u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	v ^= v >> 16u;
	v.x += v.y * v.z;
	v.y += v.z * v.x;
	v.z += v.x * v.y;
	return v;
}

// 格子ごとの 0〜1 の 3 つの乱数（周期ごとに別の並び）
float3 Random33(int3 cell, int period) {
	return float3(Hash33(uint3(cell) + uint(period) * uint3(7919u, 104729u, 1299709u))) / 4294967295.0;
}

// 格子の番号を周期 period で折り返す（負の番号も 0〜period-1 に入れる）
int3 WrapCell(int3 cell, int period) {
	return ((cell % period) + period) % period;
}

// 繰り返す Perlin ノイズ（p は 0〜1 の位置、period は 1 周期の格子の数。おおよそ -1〜1）
float TilingPerlin(float3 p, int period) {
	float3 q = p * period;
	int3 i = int3(floor(q));
	float3 f = frac(q);
	float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

	float result[8];
	[unroll]
	for (int c = 0; c < 8; ++c) {
		int3 corner = int3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
		float3 gradient = normalize(Random33(WrapCell(i + corner, period), period) * 2.0 - 1.0 + 1e-4);
		result[c] = dot(gradient, f - float3(corner));
	}
	float x00 = lerp(result[0], result[1], u.x);
	float x10 = lerp(result[2], result[3], u.x);
	float x01 = lerp(result[4], result[5], u.x);
	float x11 = lerp(result[6], result[7], u.x);
	return lerp(lerp(x00, x10, u.y), lerp(x01, x11, u.y), u.z) * 1.5;
}

// 繰り返す Perlin ノイズの重ね（0〜1）
float TilingPerlinFbm(float3 p, int period, int octaves) {
	float sum = 0.0;
	float amplitude = 1.0;
	float total = 0.0;
	for (int o = 0; o < octaves; ++o) {
		sum += TilingPerlin(p, period) * amplitude;
		total += amplitude;
		amplitude *= 0.5;
		period *= 2;
	}
	return saturate(sum / total * 0.5 + 0.5);
}

// 繰り返す Worley ノイズ（一番近い種までの距離を反転。種の近くで 1、格子の間で 0 に近い）
float TilingWorley(float3 p, int period) {
	float3 q = p * period;
	int3 i = int3(floor(q));
	float3 f = frac(q);
	float minDistance = 1.0;
	for (int z = -1; z <= 1; ++z) {
		for (int y = -1; y <= 1; ++y) {
			for (int x = -1; x <= 1; ++x) {
				int3 offset = int3(x, y, z);
				float3 seed = float3(offset) + Random33(WrapCell(i + offset, period), period);
				minDistance = min(minDistance, length(seed - f));
			}
		}
	}
	return 1.0 - saturate(minDistance);
}

// Worley ノイズを 3 段の細かさで重ねる（0〜1）
float TilingWorleyFbm(float3 p, int period) {
	return TilingWorley(p, period) * 0.625 + TilingWorley(p, period * 2) * 0.25 +
		TilingWorley(p, period * 4) * 0.125;
}

float Remap(float value, float low, float high, float newLow, float newHigh) {
	return newLow + (value - low) / (high - low) * (newHigh - newLow);
}

// Perlin ノイズの塊を、Worley ノイズの丸い粒で底上げする（Perlin-Worley）
float PerlinWorley(float3 p) {
	float perlin = TilingPerlinFbm(p, 4, 5);
	// 0.5 を境に折り返して、丸く盛り上がった塊の並び（billow）にする
	perlin = abs(perlin * 2.0 - 1.0);
	return saturate(Remap(perlin, 0.0, 1.0, TilingWorleyFbm(p, 4), 1.0));
}

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID) {
	if (any(id >= gParams.size)) {
		return;
	}
	float3 p = (float3(id) + 0.5) / float(gParams.size);

	float4 value;
	if (gParams.kind == kKindBase) {
		value = float4(PerlinWorley(p), TilingWorleyFbm(p, 4), TilingWorleyFbm(p, 8), TilingWorleyFbm(p, 16));
	} else {
		value = float4(TilingWorleyFbm(p, 2), TilingWorleyFbm(p, 4), TilingWorley(p, 8), 1.0);
	}
	gOutput[id] = value;
}
