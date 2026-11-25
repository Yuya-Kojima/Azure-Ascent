// GPUパーティクル用の構造体（C++ の ParticleCS と同じ並び。16 バイトずつの行）
struct Particle {
    float32_t3 translate;
    float32_t weight; // 重さ。重いほど空気の抵抗を受けにくく、風に乗るのが遅い
    float32_t3 scale;
    float32_t padding2;
    float32_t3 rotate;
    float32_t lifeTime;
    float32_t3 velocity;
    float32_t currentTime;
    float32_t4 color; // 今の色（Update が毎フレーム書く）
    float32_t3 scaleVelocity;
    float32_t scaleEndRate; // 寿命の終わりの大きさの倍率
    float32_t3 startScale; // 生まれたときの大きさ
    float32_t fadeStart; // 薄くなり始める寿命の割合（0〜1）
    float32_t3 colorStart; // 生まれたときの色
    float32_t padding4;
    float32_t3 colorEnd; // 寿命の終わりの色
    float32_t padding5;
    float32_t3 rotateVelocity; // 回る速さ（ラジアン/秒）
    float32_t padding6;
};
