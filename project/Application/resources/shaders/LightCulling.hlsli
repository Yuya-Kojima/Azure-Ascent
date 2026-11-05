#ifndef LIGHT_CULLING_HLSLI
#define LIGHT_CULLING_HLSLI

//=========================
// マスごとのライトの名簿の形（CS と PS で共通。C++ の TiledLightCuller・LightManager と同じ値）
//=========================

// マスの一辺の画素数
static const uint kTileSize = 16;
// 1 グループのスレッドの数（1 スレッドが 1 画素）
static const uint kTileThreadCount = kTileSize * kTileSize;

// 名簿全体の上限（LightManager::kMaxPointLights_・kMaxSpotLights_）
static const uint kMaxPointLights = 256;
static const uint kMaxSpotLights = 64;

// 1 マスの長さ（uint の数）と、その中の位置
// [0] 点の光の数 / [1] スポットライトの数 / [2]〜 点の光の番号 / その後ろ スポットライトの番号
static const uint kTileStride = 2 + kMaxPointLights + kMaxSpotLights;
static const uint kTilePointCountOffset = 0;
static const uint kTileSpotCountOffset = 1;
static const uint kTilePointIndexOffset = 2;
static const uint kTileSpotIndexOffset = 2 + kMaxPointLights;

// 画素の位置（SV_POSITION の xy）から、その画素が入るマスの名簿の先頭を出す
uint GetTileBase(float2 pixelPosition, uint tileCountX) {
	uint2 tile = uint2(pixelPosition) / kTileSize;
	return (tile.y * tileCountX + tile.x) * kTileStride;
}

#endif
