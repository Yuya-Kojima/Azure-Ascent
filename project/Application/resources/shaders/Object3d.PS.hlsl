#include "Object3d.hlsli"
#include "Common.hlsli"
#include "Lighting.hlsli"

Texture2D<float4> gTexture : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t1);
Texture2D<float32_t> gMaskTexture : register(t2);
SamplerState gSampler : register(s0);

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct PixelShaderOutput {
	float4 color : SV_TARGET0;
};

ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);

struct FogData {
    float4 color;
    float nearDist;
    float farDist;
    float enabled;
    float padding;
};
ConstantBuffer<FogData> gFog : register(b5);

//=========================
// 模様の光と傷
//=========================
static const float kPi = 3.14159265f;
// 傷から伸びる放射状のひびの本数
static const int kCrackCount = 7;
// 穴が開ききるまでに使う、開いた割合の範囲（残りでひびが伸びる）
static const float kHoleOpenPart = 0.3f;
// ひびが伸び始める、開いた割合
static const float kCrackStartOpen = 0.15f;
// ひびの暗い壁の明るさ（面の色に掛ける）
static const float kCrackWallDarkness = 0.1f;
// ひびの中の光る線の太さ（ひびの太さに対する割合）
static const float kCrackCorePart = 0.3f;
// 縁の欠けた殻の明るさ（面の色に掛ける）
static const float kRimDarkness = 0.15f;
// 縁のうち中の光を受けて光る、穴に接する部分の幅（縁の幅に対する割合）
static const float kRimLipPart = 0.25f;
// 縁の光る部分の明るさ（穴の光に対する割合）
static const float kRimLipLight = 0.8f;
// 穴の縁での光の明るさ（中心を 1 としたときの割合）
static const float kHoleEdgeLight = 0.3f;
// 穴の中のくぼみの明るさ（面の色に掛ける。縁のすぐ内側と中心）
static const float kHoleCavityEdge = 0.2f;
static const float kHoleCavityCenter = 0.05f;
// 穴の中心の白く飛ぶ点の絞り（大きいほど小さな点になる）
static const float kHoleWhitePower = 8.0f;
// 傷を描く面の向き：面の向きと傷の外向きの内積がこの範囲で、薄い→そのまま描く（0.5 ≒ 60 度）
static const float kWoundFacingFadeLow = 0.4f;
static const float kWoundFacingFadeHigh = 0.6f;

// 0〜1 の決まった乱数（同じ n なら同じ値）
float Hash11(float n) {
	return frac(sin(n * 127.1f) * 43758.5453f);
}

// 向き angle（ラジアン）の単位ベクトル
float2 AngleToDir(float angle) {
	return float2(cos(angle), sin(angle));
}

// 点 p から線分 ab までの距離
float SegmentDistance(float2 p, float2 a, float2 b) {
	float2 ab = b - a;
	float h = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-8f));
	return length(p - a - ab * h);
}

// テクスチャの水色の所だけを光らせる。水色らしさ＝min(緑, 青) − 赤（灰色・白は 0 に近い）
float3 ApplyPatternGlow(float3 color, float3 textureRgb) {
	float cyan = min(textureRgb.g, textureRgb.b) - textureRgb.r;
	float pattern = smoothstep(gMaterial.patternRange.x, gMaterial.patternRange.y, cyan);
	return color + gMaterial.patternGlow.rgb * gMaterial.patternGlow.w * pattern;
}

// 傷：傷の面（中心・外向き・面に沿った向き）に画素を投げ、中心からの距離と向きで、穴・縁の欠けた殻・ひびを描く
float3 ApplyWound(float3 color, float3 worldPosition, float3 surfaceNormal) {
	float radius = gMaterial.woundCenter.w;
	float crackLength = gMaterial.woundNormal.w;
	float open = gMaterial.woundTangent.w;
	float crackWidth = gMaterial.woundShape.x;
	float rimWidth = gMaterial.woundShape.y;
	float rimJag = gMaterial.woundShape.z;
	float spill = gMaterial.woundShape.w;
	float3 glowLight = gMaterial.woundGlow.rgb * gMaterial.woundGlow.w;

	float3 n = gMaterial.woundNormal.xyz;
	float3 t = gMaterial.woundTangent.xyz;
	float3 b = cross(n, t);
	float3 d = worldPosition - gMaterial.woundCenter.xyz;

	// 傷の面から離れた所・傷の外向きと違う向きの面（体の反対側など）には描かない
	float height = dot(d, n);
	float facing = smoothstep(kWoundFacingFadeLow, kWoundFacingFadeHigh, dot(surfaceNormal, n));
	if (abs(height) > crackLength * 0.6f || facing <= 0.0f) {
		return color;
	}
	float3 baseColor = color;
	float2 q = float2(dot(d, t), dot(d, b));
	float r = length(q);
	if (r > crackLength * 1.5f) {
		return color;
	}
	float angle = atan2(q.y, q.x);

	// 開いた割合を、穴が開く分とひびが伸びる分に分ける
	float holeOpen = saturate(open / kHoleOpenPart);
	float crackOpen = saturate((open - kCrackStartOpen) / (1.0f - kCrackStartOpen));

	// 穴の縁は、角度で半径を揺らしてぎざぎざにする（周期の違う sin を重ねる。-1〜1）
	float jag = 0.55f * sin(3.0f * angle + 1.3f) + 0.3f * sin(7.0f * angle + 0.4f) + 0.15f * sin(13.0f * angle + 2.1f);
	float holeRadius = radius * holeOpen * (1.0f + rimJag * jag);
	// 欠けてめくれた殻の外側の縁（場所によって幅が違う）
	float rimOuter = holeRadius * (1.0f + rimWidth * (0.7f + 0.3f * sin(5.0f * angle + 0.8f)));

	//---- ひび：中心から外へ 3 つの線分で折れ曲がりながら伸び、途中から短い枝が 1 本分かれる ----
	float crack = 0.0f;     // ひびの中か（0〜1）
	float crackLight = 0.0f; // ひびの中の光（根元ほど強い）
	for (int k = 0; k < kCrackCount; ++k) {
		float seed = float(k) + 1.0f;
		float baseAngle = (float(k) + 0.7f * Hash11(seed)) * (2.0f * kPi / float(kCrackCount));
		float crackEnd = lerp(radius * 1.8f, crackLength, Hash11(seed + 10.0f));
		// 折れ点の半径と向き
		float r1 = lerp(radius, crackEnd, 0.35f);
		float r2 = lerp(radius, crackEnd, 0.7f);
		float2 p0 = AngleToDir(baseAngle) * radius * 0.8f;
		float2 p1 = AngleToDir(baseAngle + (Hash11(seed + 20.0f) - 0.5f) * 0.5f) * r1;
		float2 p2 = AngleToDir(baseAngle + (Hash11(seed + 30.0f) - 0.5f) * 0.5f) * r2;
		float2 p3 = AngleToDir(baseAngle + (Hash11(seed + 40.0f) - 0.5f) * 0.6f) * crackEnd;
		float dist = min(SegmentDistance(q, p0, p1), min(SegmentDistance(q, p1, p2), SegmentDistance(q, p2, p3)));

		// 枝：1 つ目の折れ点から、左右どちらかへ斜めに
		float branchSide = Hash11(seed + 50.0f) < 0.5f ? -1.0f : 1.0f;
		float branchAngle = atan2(p1.y, p1.x) + branchSide * (0.5f + 0.4f * Hash11(seed + 60.0f));
		float2 branchEnd = p1 + AngleToDir(branchAngle) * (crackEnd - r1) * 0.45f;
		float branchDist = SegmentDistance(q, p1, branchEnd);

		// 根元（穴の縁）から先へ行くほど細くなり、先で尖って消える
		float along = saturate((r - radius) / max(crackEnd - radius, 1e-5f));
		float width = crackWidth * pow(1.0f - along, 0.8f);
		float branchWidth = width * 0.6f;
		// 開いた割合に合わせて、穴の縁から先へ伸びる
		float reach = lerp(radius, crackEnd, crackOpen);
		float visible = step(r, reach) * holeOpen;

		// 暗い壁はひびの太さいっぱい、光る線はその真ん中の細い所だけ
		float mainWall = (1.0f - smoothstep(width * 0.6f, width, dist)) * visible;
		float branchWall = (1.0f - smoothstep(branchWidth * 0.6f, branchWidth, branchDist)) * visible;
		float mainCore = (1.0f - smoothstep(width * kCrackCorePart * 0.4f, width * kCrackCorePart, dist)) * visible;
		float branchCore = (1.0f - smoothstep(branchWidth * kCrackCorePart * 0.4f, branchWidth * kCrackCorePart, branchDist)) * visible;
		crack = max(crack, max(mainWall, branchWall));
		crackLight = max(crackLight, max(mainCore, branchCore) * pow(1.0f - along, 1.5f));
	}

	//---- 穴の周りの面ににじむ光（縁から離れるほど弱い） ----
	float outside = max(r - holeRadius, 0.0f);
	float3 lit = color + glowLight * spill * exp(-outside / max(radius, 1e-5f) * 2.0f) * holeOpen;

	//---- ひび：暗い壁と、真ん中の細い光 ----
	float crackDarkness = lerp(1.0f, kCrackWallDarkness, crack);
	float3 crackGlow = glowLight * crackLight;
	color = lit * crackDarkness + crackGlow;

	//---- 縁：欠けてめくれた殻は暗く、穴に接する細い所だけ中の光を受ける。ひびと重なる所は暗いほうの暗さにする ----
	if (r >= holeRadius && r < rimOuter) {
		float ring = (r - holeRadius) / max(rimOuter - holeRadius, 1e-5f);
		float lip = 1.0f - saturate(ring / kRimLipPart);
		color = lit * min(kRimDarkness, crackDarkness) + crackGlow + glowLight * kRimLipLight * lip * lip;
	}

	//---- 穴：中心へ暗くなるくぼみに光が満ち、縁から中心へ明るくなる。中心の小さな点だけ白く飛ぶ ----
	if (r < holeRadius) {
		float core = 1.0f - r / max(holeRadius, 1e-5f);
		float3 cavity = lit * lerp(kHoleCavityEdge, kHoleCavityCenter, core);
		float whiteCore = pow(core, kHoleWhitePower) * gMaterial.woundGlow.w;
		color = cavity + glowLight * lerp(kHoleEdgeLight, 1.0f, core * core) + whiteCore.xxx;
	}
	// 向きが外れていく面ほど、もとの色に戻す
	return lerp(baseColor, color, facing);
}

PixelShaderOutput main(
GeometryShaderOutput input) {
	
	//=========================
    // UV transform & sampling
    //=========================
	float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
	float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
	
	//アルファテスト
	if (textureColor.a <= 0.5) {
		discard;
	}

	// ディゾルブ
	float edge = 0.0f;
	if (gMaterial.enableDissolve != 0) {
		float mask = gMaskTexture.Sample(gSampler, input.texcoord + gMaterial.maskTransform).r;
		if (mask <= gMaterial.dissolveThreshold) {
			discard;
		}
		// Edgeっぽさを算出
		edge = 1.0f - smoothstep(gMaterial.dissolveThreshold, gMaterial.dissolveThreshold + gMaterial.dissolveEdgeRange, mask);
	}

	//=========================
    // Base color
    //=========================
	float4 base = gMaterial.color * textureColor;
	
	PixelShaderOutput output;
	
    //=========================
    // No lighting (Emissive / Laser)
    //=========================
	if (gMaterial.enableLighting == 0) {
		float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
		float3 normal = normalize(input.normal);
		// 視線と法線の角度からフレネル係数を計算（中心部ほど大きい）
		float NdotV = saturate(abs(dot(normal, toEye)));

		// 中心コア（純白）と外側（マテリアルカラー）のグラデーション
		float coreFactor = pow(NdotV, 3.0f);
		float3 core = float3(2.5f, 2.5f, 2.5f) * coreFactor;
		float3 aura = base.rgb * (pow(1.0f - NdotV, 0.8f) + 0.4f);

		output.color = float4(core + aura, base.a);
		return output;
	}
	
	//=========================
	// Common vectors
	//=========================
	float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
	float3 normal = normalize(input.normal);

	// 調べるための表示（平行光源の debugView で切り替える）
	if (gDirectionalLight.debugView == 1) {
		output.color = float4(gMaterial.color.rgb * textureColor.rgb, 1.0f);
		return output;
	}
	if (gDirectionalLight.debugView == 2) {
		output.color = float4(normal * 0.5f + 0.5f, 1.0f);
		return output;
	}
	if (gDirectionalLight.debugView == 3) {
		output.color = dot(normal, toEye) >= 0.0f ? float4(0.0f, 0.6f, 0.0f, 1.0f) : float4(1.0f, 0.0f, 0.0f, 1.0f);
		return output;
	}
	if (gDirectionalLight.debugView == 4) {
		textureColor.rgb = float3(1.0f, 1.0f, 1.0f);
	}
	if (gDirectionalLight.debugView == 5) {
		// 法線が壊れている画素（NaN・長さ0）はマゼンタ、それ以外は灰色で塗り、捨てずに表示する
		bool brokenNormal = any(isnan(input.normal)) || dot(input.normal, input.normal) < 1e-8f;
		output.color = brokenNormal ? float4(1.0f, 0.0f, 1.0f, 1.0f) : float4(0.5f, 0.5f, 0.5f, 1.0f);
		return output;
	}

	float3 baseColor = gMaterial.color.rgb * textureColor.rgb;
		
	//=========================
	// Lighting Calculations
	//=========================
	LightResult dirLight = CalculateDirectionalLight(gDirectionalLight, normal, toEye, gMaterial.shininess, baseColor);
	LightResult pointLight = CalculatePointLight(gPointLight, input.worldPosition, normal, toEye, gMaterial.shininess, baseColor);
	LightResult spotLight = CalculateSpotLight(gSpotLight, input.worldPosition, normal, toEye, gMaterial.shininess, baseColor);

	//=========================
	// Output
	//=========================
	float3 specular = (dirLight.specular + pointLight.specular + spotLight.specular) * gMaterial.specularScale;
	output.color.rgb = dirLight.diffuse + pointLight.diffuse + spotLight.diffuse + specular;

	// 環境光
	output.color.rgb += CalculateHemisphereAmbient(gDirectionalLight, normal, baseColor);
	
	// Environment Map（係数が0より大きいときだけ読んで足す）
	if (gMaterial.environmentCoefficient > 0.0f) {
		float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
		float3 reflectedVector = reflect(cameraToPosition, normalize(input.normal));
		float4 environmentColor = gEnvironmentTexture.Sample(gSampler, reflectedVector);
		output.color.rgb += environmentColor.rgb * gMaterial.environmentCoefficient;
	}

	// Dissolve Edge 発光
	if (gMaterial.enableDissolve != 0) {
		output.color.rgb += edge * gMaterial.dissolveEdgeColor.rgb;
	}

	// Rim Light（縁の発光。強度0なら無効）
	if (gMaterial.rimColor.w > 0.0f) {
		float rim = pow(saturate(1.0f - saturate(dot(normal, toEye))), max(gMaterial.rimPower, 0.01f));
		output.color.rgb += gMaterial.rimColor.rgb * gMaterial.rimColor.w * rim;
	}

	// 逆光の縁（平行光源の設定にマテリアルの倍率を掛ける。倍率0なら無効）
	if (gMaterial.backRimScale > 0.0f) {
		output.color.rgb += CalculateBackRim(gDirectionalLight, normal, toEye, gMaterial.backRimScale);
	}

	// 模様の光（強さ0なら無効）
	if (gMaterial.patternGlow.w > 0.0f) {
		output.color.rgb = ApplyPatternGlow(output.color.rgb, textureColor.rgb);
	}

	// 傷（開いた割合0なら無効）
	if (gMaterial.woundTangent.w > 0.0f) {
		output.color.rgb = ApplyWound(output.color.rgb, input.worldPosition, normal);
	}

	// Fog
	if (gFog.enabled != 0.0f) {
		float dist = length(gCamera.worldPosition - input.worldPosition);
		float fogFactor = saturate((dist - gFog.nearDist) / (gFog.farDist - gFog.nearDist));
		output.color.rgb = lerp(output.color.rgb, gFog.color.rgb, fogFactor);
	}

	output.color.a = gMaterial.color.a * textureColor.a;
	
	// 計算結果がNaNやInfになったピクセルは描画を破棄する
	if (any(isnan(output.color)) || any(isinf(output.color))) {
		discard;
	}
	
	return output;
}

