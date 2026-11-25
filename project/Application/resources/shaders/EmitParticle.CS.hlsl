#include "Particle.CS.hlsl"

// 出す頼み 1 つ（C++ の EmitterData と同じ並び。16 バイトずつの行）
struct EmitterData {
	float32_t3 translate; // 出す中心
	float32_t weightMin; // 重さの最小

	float32_t3 spawnArea; // 中心から散らす範囲（半分の大きさ）
	float32_t weightMax; // 重さの最大

	uint32_t count; // 出す数
	float32_t padding0;
	float32_t padding1;
	float32_t padding2;

	float32_t3 baseColor;
	float32_t lifeMin;

	float32_t3 baseVelocity;
	float32_t lifeMax;

	float32_t3 velocityRandom;
	float32_t fadeStart; // 薄くなり始める寿命の割合

	float32_t3 baseScale;
	float32_t padding3;

	float32_t3 scaleRandom;
	float32_t padding4;

	float32_t3 baseRotate;
	float32_t padding5;

	float32_t3 rotateRandom;
	float32_t padding6;

	float32_t3 scaleVelocity;
	float32_t isConverge;

	float32_t3 colorEnd; // 寿命の終わりの色
	float32_t scaleEndRate; // 寿命の終わりの大きさの倍率

	float32_t4x4 boxWorld; // 箱の中 → ワールド（回転と位置だけ）

	float32_t3 boxHalfSize; // 箱の半分の大きさ（m）
	uint32_t emitShape; // 出す場所の形（0: 中心のまわりの箱の中、1: 箱の表面の距離の範囲）

	float32_t3 bandCenter; // 距離を測る点
	float32_t bandInner; // 出す距離の範囲の内側（m）

	float32_t bandOuter; // 出す距離の範囲の外側（m）
	float32_t surfaceSpeed; // 面の外向きにはがれる速さ（m/秒）
	float32_t padding7;
	float32_t padding8;

	float32_t3 rotateVelocityRandom; // 回る速さのばらつき（ラジアン/秒）
	float32_t padding9;
};

static const uint32_t kEmitShapeBoxSurface = 1;
// 箱の表面で、距離の範囲に入る点を探す回数
static const uint32_t kSurfaceTries = 8;

struct PerFrame {
	float32_t time;
	float32_t deltaTime;
};

StructuredBuffer<EmitterData> gRequests : register(t0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);

// --- 乱数（PCG ハッシュ） ---
// 32 ビットの整数をかき混ぜて、別の 32 ビットの整数にする
uint32_t PcgHash(uint32_t value) {
	uint32_t state = value * 747796405u + 2891336453u;
	uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// 種をかき混ぜ直し、0〜1 の乱数を 1 つ返す
float32_t Random01(inout uint32_t seed) {
	seed = PcgHash(seed);
	return float32_t(seed) / 4294967295.0f;
}

// 0〜1 の乱数を 3 つ返す
float32_t3 Random3(inout uint32_t seed) {
	float32_t x = Random01(seed);
	float32_t y = Random01(seed);
	float32_t z = Random01(seed);
	return float32_t3(x, y, z);
}

// 箱の表面の点を 1 つ選ぶ（面は面積に比例した確率で選ぶ）
// localNormal に、選んだ面の外向き（箱の中の向き）を返す
float32_t3 RandomBoxSurfacePoint(float32_t3 halfSize, inout uint32_t seed, out float32_t3 localNormal) {
	// 向かい合う 2 面の、1 面の面積の 1/4（±X 面は y×z、±Y 面は x×z、±Z 面は x×y）
	float32_t areaX = halfSize.y * halfSize.z;
	float32_t areaY = halfSize.x * halfSize.z;
	float32_t areaZ = halfSize.x * halfSize.y;
	float32_t pick = Random01(seed) * (areaX + areaY + areaZ);
	float32_t side = Random01(seed) < 0.5f ? -1.0f : 1.0f;
	// 面の上の点は、箱の -1〜1 の座標で選んでから半分の大きさをかける
	float32_t3 p = (Random3(seed) - 0.5f) * 2.0f;
	if (pick < areaX) {
		p.x = side;
		localNormal = float32_t3(side, 0.0f, 0.0f);
	} else if (pick < areaX + areaY) {
		p.y = side;
		localNormal = float32_t3(0.0f, side, 0.0f);
	} else {
		p.z = side;
		localNormal = float32_t3(0.0f, 0.0f, side);
	}
	return p * halfSize;
}

// X: 頼みの中の何粒目か、Y: 何番目の頼みか。1 スレッドが 1 粒を出す
[numthreads(256, 1, 1)]
void main(uint32_t3 DTid : SV_DispatchThreadID) {
	uint32_t particleInRequest = DTid.x;
	uint32_t requestIndex = DTid.y;
	EmitterData request = gRequests[requestIndex];

	// 一番多い頼みに合わせてスレッドを出しているので、この頼みの数を超えたスレッドは何もしない
	if (particleInRequest >= request.count) {
		return;
	}

	// 種: 頼みの中の何粒目か・何番目の頼みか・時刻（ビットのまま）を混ぜる
	uint32_t seed = PcgHash(particleInRequest ^ PcgHash(requestIndex ^ PcgHash(asuint(gPerFrame.time))));

	// 箱の表面から出すときは、先に出す場所を決める。距離の範囲に入る点が見つからなければ出さない
	float32_t3 surfacePosition = float32_t3(0.0f, 0.0f, 0.0f);
	float32_t3 surfaceNormal = float32_t3(0.0f, 0.0f, 0.0f);
	if (request.emitShape == kEmitShapeBoxSurface) {
		bool found = false;
		for (uint32_t tryIndex = 0; tryIndex < kSurfaceTries && !found; ++tryIndex) {
			float32_t3 localNormal;
			float32_t3 localPoint = RandomBoxSurfacePoint(request.boxHalfSize, seed, localNormal);
			float32_t3 worldPoint = mul(float32_t4(localPoint, 1.0f), request.boxWorld).xyz;
			float32_t distanceFromCenter = length(worldPoint - request.bandCenter);
			if (request.bandInner <= distanceFromCenter && distanceFromCenter <= request.bandOuter) {
				surfacePosition = worldPoint;
				surfaceNormal = mul(float32_t4(localNormal, 0.0f), request.boxWorld).xyz;
				found = true;
			}
		}
		if (!found) {
			return;
		}
	}

	// 空きの列の一番上を 1 つ取る（ほかのスレッドと同時に取るので InterlockedAdd で数を減らす）
	int32_t freeListIndex;
	InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
	if (freeListIndex < 0) {
		// 空きがなかったので、減らした分を戻して出さない
		InterlockedAdd(gFreeListIndex[0], 1);
		return;
	}
	uint32_t particleIndex = gFreeList[freeListIndex];

	Particle particle = (Particle)0;

	// 大きさは baseScale + scaleRandom * (0〜1)
	particle.scale = request.baseScale + Random3(seed) * request.scaleRandom;

	// 回転は baseRotate + rotateRandom * (-1〜1)
	particle.rotate = request.baseRotate + (Random3(seed) - 0.5f) * 2.0f * request.rotateRandom;

	// 位置: 箱の表面なら選んだ点。そうでなければ中心から spawnArea の範囲（-1〜1 倍）に散らす
	float32_t3 offset = (Random3(seed) - 0.5f) * 2.0f * request.spawnArea;
	if (request.emitShape == kEmitShapeBoxSurface) {
		particle.translate = surfacePosition;
	} else {
		particle.translate = request.translate + offset;
	}

	particle.lifeTime = lerp(request.lifeMin, request.lifeMax, Random01(seed));

	float32_t3 randomVelocity = Random3(seed);
	if (request.isConverge > 0.5f) {
		// 収束モード：寿命の終わりぴったりに中心（offset=0）に到達する速度
		particle.velocity = -offset / particle.lifeTime;
	} else {
		// 通常モード：baseVelocity + 面の外向き × surfaceSpeed + velocityRandom * (-1〜1)
		particle.velocity = request.baseVelocity + surfaceNormal * request.surfaceSpeed +
			(randomVelocity - 0.5f) * 2.0f * request.velocityRandom;
	}

	particle.currentTime = 0.0f;
	particle.color = float32_t4(request.baseColor, 1.0f);
	particle.scaleVelocity = request.scaleVelocity;

	// 重さは最小〜最大の間で粒ごとに決める
	particle.weight = lerp(request.weightMin, request.weightMax, Random01(seed));
	// 生まれたときの色と大きさを覚え、寿命の間に終わりの色・倍率へ変える
	particle.colorStart = request.baseColor;
	particle.colorEnd = request.colorEnd;
	particle.startScale = particle.scale;
	particle.scaleEndRate = request.scaleEndRate;
	particle.fadeStart = request.fadeStart;
	// 回る速さは rotateVelocityRandom * (-1〜1)
	particle.rotateVelocity = (Random3(seed) - 0.5f) * 2.0f * request.rotateVelocityRandom;

	gParticles[particleIndex] = particle;
}
