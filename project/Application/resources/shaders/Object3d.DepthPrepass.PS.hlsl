#include "Object3d.hlsli"
#include "Common.hlsli"

// 深度だけの描画。色は出さず、深度だけの絵に深さを書く。
// 守る決まり: 深度だけの絵の深さ ≥ 色が見えている画素の深さ。
// 後ろが透けて見える画素（半透明・ディゾルブ）と、色の描画で捨てられる画素は深度を書かない

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);

void main(GeometryShaderOutput input) {
	float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
	float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

	// 少しでも透ける画素（テクスチャの縁の柔らかい所を含む）
	if (gMaterial.color.a * textureColor.a < 1.0f) {
		discard;
	}

	// ディゾルブ中の物は、消えかけの穴から後ろが見えるので全部書かない
	if (gMaterial.enableDissolve != 0) {
		discard;
	}

	// 法線が壊れている画素は、色の描画で NaN になって捨てられる
	if (any(isnan(input.normal)) || dot(input.normal, input.normal) < 1e-8f) {
		discard;
	}
}
