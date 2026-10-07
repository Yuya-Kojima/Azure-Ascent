#ifndef COMMON_HLSLI
#define COMMON_HLSLI

//=========================
// 共通のデータ構造体
//=========================
struct Material {
	float4 color;
	int enableLighting;
	float3 padding;
	float4x4 uvTransform;
	float shininess;
	float environmentCoefficient;
	int enableDissolve;
	float dissolveThreshold;
	float dissolveEdgeRange;
	float2 maskTransform;
	float padding2;
	float4 dissolveEdgeColor;
	float4 rimColor;
	float rimPower;
	float3 padding3;
};

struct Camera {
	float3 worldPosition;
};

#endif
