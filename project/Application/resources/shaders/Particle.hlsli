struct VertexShaderOutput {
	float4 position : SV_POSITION;
	float2 texcoord : TEXCOORD0;
	float4 color : COLOR0;
	float fireAge : TEXCOORD1;        // 炎の描き方で使う。寿命の進み具合（0〜1）
	float2 fireNoisePos : TEXCOORD2;  // 炎の描き方で使う。模様の原点から見た、画面の右・下向きの位置（m）
	float3 streakPos : TEXCOORD3;     // 速さで伸ばす描き方で使う。x: 横の位置、y: 伸びる向きの位置、z: 半分の長さ（どれも線の半分の太さを 1 とする）
};

struct PerView {
    float4x4 viewProjection;
    float4x4 billboardMatrix;
    float3 cameraPosition;
    float padding;
};

// Material の isBillboard の値
static const int kBillboardSpin = 2;   // カメラへ向けた板の中で、粒の回転もかける
static const int kBillboardStreak = 3; // 画面の上で、飛ぶ向きへ速さに合わせて伸ばした線にする

// Material の isFireMode の値
static const int kFireOff = 0;   // 炎の描き方にしない
static const int kFireFlame = 1; // 燃え続ける炎（上へ伸びる舌の形）
static const int kFireBall = 2;  // 火の玉（丸く、縁のやわらかいもや）

struct Material {
    float4 color;
    int enableLighting;
    int isBillboard;
    int isRingMode;
    int isFireMode; // 炎の描き方（模様を寿命で削り、温度の色で塗る）。kFireOff・kFireFlame・kFireBall
    float4x4 uvTransform;
    float3 fireNoiseOrigin; // 炎の模様を置く原点（ワールド座標）
    float fireTime;         // 炎の模様を流す時間（秒）
    float streakSeconds;    // 速さで伸ばす描き方で、線の長さを「速さ × この秒数」にする
    float streakWidthRate;  // 速さで伸ばす描き方で、線の半分の太さを「粒の大きさ × この倍率」にする
    float2 padding;
};