#include "Lighting.hlsli"
#include "LightCulling.hlsli"

// マス分け: 1 グループが 1 マス（16×16 画素）、1 スレッドが 1 画素。
// マスの 4 辺の面と、near 〜 マスの中で一番遠い深さで囲んだ範囲に、届く範囲の球が少しでも入るライトを名簿に書く

struct CullingParams {
	float4x4 view;              // デフォルトカメラの view 行列
	float4x4 projectionInverse; // 射影の逆行列
	float nearZ;                // 深さ 0 を逆射影したカメラ空間の奥行き
	uint tileCountX;
	uint tileCountY;
	uint pointLightCount;
	uint spotLightCount;
	float screenWidth;
	float screenHeight;
	float padding;
};

ConstantBuffer<CullingParams> gParams : register(b0);
StructuredBuffer<PointLight> gPointLights : register(t0);
StructuredBuffer<SpotLight> gSpotLights : register(t1);
Texture2D<float> gDepth : register(t2);
RWStructuredBuffer<uint> gTileLights : register(u0);

// マスの中で一番遠い深さ（0〜1 の float を uint として持つ。正の float は uint にしても大小の順が同じ）
groupshared uint gsMaxDepth;
// マスの 4 辺の面の内向きの法線（面はカメラの位置＝原点を通る）
groupshared float3 gsPlaneNormals[4];
// マスの奥行きの奥の端（カメラ空間の z）
groupshared float gsMaxZ;
// このマスに入るライトの数と番号
groupshared uint gsPointCount;
groupshared uint gsSpotCount;
groupshared uint gsPointIndices[kMaxPointLights];
groupshared uint gsSpotIndices[kMaxSpotLights];

// 画素の位置を、遠い面（NDC の z = 1）の上の点としてカメラ空間に直す
float3 PixelToViewOnFarPlane(float2 pixel) {
	float2 ndc = float2(pixel.x / gParams.screenWidth * 2.0f - 1.0f,
	                    1.0f - pixel.y / gParams.screenHeight * 2.0f); // 画素の y は下向き、NDC の y は上向き
	float4 view = mul(float4(ndc, 1.0f, 1.0f), gParams.projectionInverse);
	return view.xyz / view.w;
}

// 深さ（0〜1）をカメラ空間の奥行き z に直す
float DepthToViewZ(float depth) {
	float4 view = mul(float4(0.0f, 0.0f, depth, 1.0f), gParams.projectionInverse);
	return view.z / view.w;
}

// カメラ空間の球（中心 center・半径 radius）が、マスの 4 面の内側と奥行きの範囲に少しでも入るか
bool IsSphereInTile(float3 center, float radius) {
	[unroll]
	for (uint i = 0; i < 4; ++i) {
		if (dot(gsPlaneNormals[i], center) < -radius) {
			return false;
		}
	}
	return center.z + radius >= gParams.nearZ && center.z - radius <= gsMaxZ;
}

[numthreads(kTileSize, kTileSize, 1)]
void main(uint3 groupId : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex) {
	if (groupIndex == 0) {
		gsMaxDepth = 0;
		gsPointCount = 0;
		gsSpotCount = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	//---- 1. 各スレッドが自分の画素の深さを読み、マスの中の最大に入れる ----
	uint2 pixel = dispatchThreadId.xy;
	if (pixel.x < uint(gParams.screenWidth) && pixel.y < uint(gParams.screenHeight)) {
		float depth = gDepth.Load(int3(pixel, 0));
		InterlockedMax(gsMaxDepth, asuint(depth));
	}
	GroupMemoryBarrierWithGroupSync();

	//---- 2. スレッド 0 が、マスの 4 辺の面と奥行きの奥の端を作る ----
	if (groupIndex == 0) {
		float2 tileMin = float2(groupId.xy * kTileSize);
		float2 tileMax = tileMin + float(kTileSize);

		// マスの 4 つの角を遠い面の上の点にする
		float3 topLeft = PixelToViewOnFarPlane(float2(tileMin.x, tileMin.y));
		float3 topRight = PixelToViewOnFarPlane(float2(tileMax.x, tileMin.y));
		float3 bottomLeft = PixelToViewOnFarPlane(float2(tileMin.x, tileMax.y));
		float3 bottomRight = PixelToViewOnFarPlane(float2(tileMax.x, tileMax.y));
		float3 center = PixelToViewOnFarPlane((tileMin + tileMax) * 0.5f);

		// 隣り合う 2 つの角と原点を通る面。法線はマスの中心の側（内側）を向くようにそろえる
		float3 normals[4] = {
			normalize(cross(topLeft, bottomLeft)),    // 左
			normalize(cross(bottomRight, topRight)),  // 右
			normalize(cross(topRight, topLeft)),      // 上
			normalize(cross(bottomLeft, bottomRight)) // 下
		};
		[unroll]
		for (uint i = 0; i < 4; ++i) {
			gsPlaneNormals[i] = dot(normals[i], center) < 0.0f ? -normals[i] : normals[i];
		}

		gsMaxZ = DepthToViewZ(asfloat(gsMaxDepth));
	}
	GroupMemoryBarrierWithGroupSync();

	//---- 3. 256 スレッドでライトを分担し、入るライトの番号を名簿に書く ----
	for (uint pointIndex = groupIndex; pointIndex < gParams.pointLightCount; pointIndex += kTileThreadCount) {
		PointLight light = gPointLights[pointIndex];
		float3 center = mul(float4(light.position, 1.0f), gParams.view).xyz;
		if (IsSphereInTile(center, light.radius)) {
			uint slot;
			InterlockedAdd(gsPointCount, 1, slot);
			gsPointIndices[slot] = pointIndex;
		}
	}
	// スポットライトは向き・角度を見ず、光の元を中心に届く距離を半径にした球で囲む
	for (uint spotIndex = groupIndex; spotIndex < gParams.spotLightCount; spotIndex += kTileThreadCount) {
		SpotLight light = gSpotLights[spotIndex];
		float3 center = mul(float4(light.position, 1.0f), gParams.view).xyz;
		if (IsSphereInTile(center, light.distance)) {
			uint slot;
			InterlockedAdd(gsSpotCount, 1, slot);
			gsSpotIndices[slot] = spotIndex;
		}
	}
	GroupMemoryBarrierWithGroupSync();

	//---- 4. groupshared の名簿をマスの名簿へ写す（毎フレーム全マスを書くので、クリアは要らない） ----
	uint tileBase = (groupId.y * gParams.tileCountX + groupId.x) * kTileStride;
	for (uint i = groupIndex; i < gsPointCount; i += kTileThreadCount) {
		gTileLights[tileBase + kTilePointIndexOffset + i] = gsPointIndices[i];
	}
	for (uint j = groupIndex; j < gsSpotCount; j += kTileThreadCount) {
		gTileLights[tileBase + kTileSpotIndexOffset + j] = gsSpotIndices[j];
	}
	if (groupIndex == 0) {
		gTileLights[tileBase + kTilePointCountOffset] = gsPointCount;
		gTileLights[tileBase + kTileSpotCountOffset] = gsSpotCount;
	}
}
