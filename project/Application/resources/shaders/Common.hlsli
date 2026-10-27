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
	float backRimScale;
	float specularScale;
	float padding3;
	float4 patternGlow;  // rgb=色, w=強さ（0で無効）
	float4 patternRange; // x=光り始める水色らしさ, y=光りきる水色らしさ
	float4 woundCenter;  // xyz=ワールド座標, w=穴の半径
	float4 woundNormal;  // xyz=外向き, w=ひびの長さ
	float4 woundTangent; // xyz=面に沿った向き, w=開いた割合（0で傷なし）
	float4 woundGlow;    // rgb=色, w=強さ
	float4 woundShape;   // x=ひびの太さ, y=縁の殻の幅の割合, z=縁のぎざぎざの割合, w=周りににじむ光
};

struct Camera {
	float3 worldPosition;
};

#endif
