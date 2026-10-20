//=========================
// 雲の層の上の面の起伏に使う 2 次元ノイズを作る（端どうしがつながる＝繰り返して貼れる）
// 1 枚に塊が kTopNoiseCells × kTopNoiseCells 個並ぶ。値は 0〜1
//=========================

struct NoiseParams {
	uint size; // テクスチャの一辺の画素数
	uint kind; // 使わない（3 次元ノイズと同じ並びにそろえる）
};

ConstantBuffer<NoiseParams> gParams : register(b0);
RWTexture2D<float> gOutput : register(u0);

// 1 枚の中の、一番大きな塊の数（一辺）。CloudSea.hlsli の kTopNoiseCells と同じ
static const int kTopNoiseCells = 16;
// 重ねる回数（回ごとに細かさを倍、強さを半分）
static const int kOctaves = 5;

// 整数の 2 次元ハッシュ（0〜1）
float Hash21(uint2 v, uint octave) {
	v = v * 1664525u + 1013904223u + octave * 2654435761u;
	v.x += v.y * 1664525u;
	v.y += v.x * 1664525u;
	v ^= v >> 16u;
	v.x += v.y * 1664525u;
	v.y += v.x * 1664525u;
	v ^= v >> 16u;
	return float(v.x ^ v.y) / 4294967295.0;
}

// 繰り返す 2 次元 Value Noise（p は 0〜1 の位置、period は 1 周期の格子の数。3 次の補間。0〜1）
float TilingValueNoise(float2 p, int period, uint octave) {
	float2 q = p * period;
	int2 i = int2(floor(q));
	float2 f = frac(q);
	f = f * f * (3.0 - 2.0 * f);
	int2 i0 = ((i % period) + period) % period;
	int2 i1 = (i0 + 1) % period;
	float n00 = Hash21(uint2(i0.x, i0.y), octave);
	float n10 = Hash21(uint2(i1.x, i0.y), octave);
	float n01 = Hash21(uint2(i0.x, i1.y), octave);
	float n11 = Hash21(uint2(i1.x, i1.y), octave);
	return lerp(lerp(n00, n10, f.x), lerp(n01, n11, f.x), f.y);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
	if (any(id.xy >= gParams.size)) {
		return;
	}
	float2 p = (float2(id.xy) + 0.5) / float(gParams.size);

	float sum = 0.0;
	float total = 0.0;
	float amplitude = 0.5;
	int period = kTopNoiseCells;
	for (int o = 0; o < kOctaves; ++o) {
		sum += amplitude * TilingValueNoise(p, period, uint(o));
		total += amplitude;
		amplitude *= 0.5;
		period *= 2;
	}
	gOutput[id.xy] = sum / total;
}
