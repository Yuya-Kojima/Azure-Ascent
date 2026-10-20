#include "CloudSea.hlsli"

struct VertexShaderInput {
	float2 position : POSITION0; // 網の中心からの水平の位置
};

// カメラからの水平の距離に対する網の目の幅のおおよその比（段の内側の縁 32 目・外側の縁 64 目の中ほど）
static const float kGridSpacingPerDistance = 1.0 / 48.0;
// 一番細かい網の目の幅
static const float kFinestSpacing = 1.0;
// 描く一番細かい起伏の波長（網の目の幅に対する倍率）
static const float kMinWavelengthPerSpacing = 2.0;

CloudSeaVSOutput main(VertexShaderInput input) {
	CloudSeaVSOutput output;

	float2 xz = gCloudSea.gridOrigin + input.position;

	// その場所の網の目の幅（カメラから遠いほど粗い）
	float cameraDistance = length(xz - gCloudSea.cameraPosition.xz);
	float spacing = max(cameraDistance * kGridSpacingPerDistance, kFinestSpacing);
	float minWavelength = spacing * kMinWavelengthPerSpacing;

	float baseHeight = CloudSeaBaseHeight(xz, minWavelength);
	float y = gCloudSea.height + CloudSeaApplyShapes(baseHeight, xz);

	// 法線：少しずらした 2 点の高さの差から求める
	float e = spacing * 0.5;
	float hx = CloudSeaHeight(xz + float2(e, 0.0), minWavelength);
	float hz = CloudSeaHeight(xz + float2(0.0, e), minWavelength);
	output.normal = normalize(float3(-(hx - y) / e, 1.0, -(hz - y) / e));

	output.worldPosition = float3(xz.x, y, xz.y);
	output.position = mul(float4(output.worldPosition, 1.0), gCloudSea.viewProjection);
	output.baseHeight = baseHeight;
	return output;
}
