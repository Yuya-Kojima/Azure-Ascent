#ifndef SKY_CLEARING_HLSLI
#define SKY_CLEARING_HLSLI

//=========================
// 空の雲のノイズと晴れの円（空と雲海で同じ式を使う）
//=========================

// 格子点の整数座標から 0〜1 の値を作るハッシュ（整数の掛け算と排他的論理和だけで計算する）
float Hash13(int3 cell) {
	uint3 q = asuint(cell) * uint3(1597334673u, 3812015801u, 2798796415u);
	uint n = (q.x ^ q.y ^ q.z) * 1597334673u;
	return float(n) * (1.0 / 4294967295.0);
}

// 3次元Value Noise（5次エルミート補間）
float ValueNoise3D(float3 p) {
	float3 cellFloor = floor(p);
	int3 i = int3(cellFloor);
	float3 f = p - cellFloor;
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

	float n000 = Hash13(i + int3(0, 0, 0));
	float n100 = Hash13(i + int3(1, 0, 0));
	float n010 = Hash13(i + int3(0, 1, 0));
	float n110 = Hash13(i + int3(1, 1, 0));
	float n001 = Hash13(i + int3(0, 0, 1));
	float n101 = Hash13(i + int3(1, 0, 1));
	float n011 = Hash13(i + int3(0, 1, 1));
	float n111 = Hash13(i + int3(1, 1, 1));

	float nx00 = lerp(n000, n100, f.x);
	float nx10 = lerp(n010, n110, f.x);
	float nx01 = lerp(n001, n101, f.x);
	float nx11 = lerp(n011, n111, f.x);

	float nxy0 = lerp(nx00, nx10, f.y);
	float nxy1 = lerp(nx01, nx11, f.y);

	return lerp(nxy0, nxy1, f.z);
}

// 3次元FBM（全天球どこでも歪みや直線の筋が出ない有機的な雲）
float Fbm3D(float3 p) {
	float v = 0.0;
	float a = 0.5;
	const float3x3 rot = float3x3(
		 0.00,  0.80,  0.60,
		-0.80,  0.36, -0.48,
		-0.60, -0.48,  0.64
	);
	for (int i = 0; i < 4; ++i) {
		v += a * ValueNoise3D(p);
		p = mul(rot, p) * 2.02 + float3(17.0, 31.0, 47.0);
		a *= 0.5;
	}
	return v;
}

// 空の雲のノイズ（dir の向きの雲の濃さのもとになる値）。time は空の時刻（秒）
float SkyCloudNoise(float3 dir, float time) {
	float3 windOffset = float3(time * 0.015, 0.0, time * 0.010);
	return Fbm3D(dir * 3.2 + windOffset);
}

// 晴れの円の境目から外側へ測った角度（ラジアン。内側で負）。
// circle は xyz: 円の中心の向き, w: 半径（ラジアン）。境目を雲のノイズ n で noiseWidth の幅だけ内外へ揺らす
float ClearCircleOutside(float3 dir, float4 circle, float noiseWidth, float n) {
	float angle = acos(clamp(dot(dir, normalize(circle.xyz)), -1.0, 1.0));
	angle += (n - 0.5) * 2.0 * noiseWidth;
	return angle - circle.w;
}

// 晴れた空の割合：境目の内側 softness の幅で 1 → 0
float ClearCircleRatio(float outside, float softness) {
	return 1.0 - smoothstep(-max(softness, 1e-4), 0.0, outside);
}

// 押しやられた闇の濃さ：境目の上で 1、外側 darkWidth の幅で 0
float ClearCirclePush(float outside, float darkWidth) {
	return 1.0 - smoothstep(0.0, max(darkWidth, 1e-4), max(outside, 0.0));
}

#endif
