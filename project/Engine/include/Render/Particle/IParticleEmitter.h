#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Math/MathUtil.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector4.h"
#include "Particle/Particle.h"
#include <string>
#include <list>
#include <optional>
#include <random>
#include <vector>
#include <wrl.h>

// 出す頼み 1 つ（EmitParticle.CS の gRequests の 1 要素。16 バイトずつの行）
struct EmitterData {
    Vector3 translate;       // 12 bytes
    float weightMin;         // 4 bytes  (16 bytes) 重さの最小

    Vector3 spawnArea;       // 12 bytes
    float weightMax;         // 4 bytes  (16 bytes) 重さの最大

    uint32_t count;          // 4 bytes  出す数
    float padding0;          // 4 bytes
    float padding1;          // 4 bytes
    float padding2;          // 4 bytes  (16 bytes)

    Vector3 baseColor;       // 12 bytes
    float lifeMin;           // 4 bytes  (16 bytes)

    Vector3 baseVelocity;    // 12 bytes
    float lifeMax;           // 4 bytes  (16 bytes)

    Vector3 velocityRandom;  // 12 bytes
    float fadeStart;         // 4 bytes  (16 bytes) 薄くなり始める寿命の割合

    Vector3 baseScale;       // 12 bytes
    float padding3;          // 4 bytes  (16 bytes)

    Vector3 scaleRandom;     // 12 bytes
    float padding4;          // 4 bytes  (16 bytes)

    Vector3 baseRotate;      // 12 bytes
    float padding5;          // 4 bytes  (16 bytes)

    Vector3 rotateRandom;    // 12 bytes
    float padding6;          // 4 bytes  (16 bytes)

    Vector3 scaleVelocity;   // 12 bytes
    float isConverge;        // 4 bytes  (16 bytes)

    Vector3 colorEnd;        // 12 bytes 寿命の終わりの色
    float scaleEndRate;      // 4 bytes  (16 bytes) 寿命の終わりの大きさの倍率

    Matrix4x4 boxWorld;      // 64 bytes 箱の中 → ワールド（回転と位置だけ）

    Vector3 boxHalfSize;     // 12 bytes 箱の半分の大きさ（m）
    uint32_t emitShape;      // 4 bytes  (16 bytes) 出す場所の形（ParticleEmitShape）

    Vector3 bandCenter;      // 12 bytes 距離を測る点
    float bandInner;         // 4 bytes  (16 bytes) 出す距離の範囲の内側（m）

    float bandOuter;         // 4 bytes  出す距離の範囲の外側（m）
    float surfaceSpeed;      // 4 bytes  面の外向きにはがれる速さ（m/秒）
    float padding7;          // 4 bytes
    float padding8;          // 4 bytes  (16 bytes)

    Vector3 rotateVelocityRandom; // 12 bytes 回る速さのばらつき（ラジアン/秒）
    float padding9;          // 4 bytes  (16 bytes)
};

// 粒を出す場所の形
enum class ParticleEmitShape : uint32_t {
    Volume = 0,  // 中心のまわりの spawnRandom の箱の中
    BoxSurface = 1, // 箱の表面のうち、bandCenter からの距離が bandInner〜bandOuter の所
};

// 入れ物ごとの、粒にかかる力の設定（UpdateParticle.CS の b1）
struct ParticleMotion {
    Vector3 gravity;         // 12 bytes 重力の加速度（m/秒²）
    float drag;              // 4 bytes  (16 bytes) 空気の抵抗の強さ（1/秒。重さ 1 の粒が風の速さへ近づく割合）

    Vector3 wind;            // 12 bytes 風の速さ（m/秒）
    float padding0;          // 4 bytes  (16 bytes)
};

struct ParticleForGPU {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Vector4 color;
};

struct ParticleEmitDesc {
    Vector3 position;
    uint32_t count;
    Vector3 baseVelocity;
    Vector3 velocityRandom;
    Vector3 spawnRandom;
    float lifeMin;
    float lifeMax;
    Vector4 color;
    Vector3 baseScale = {1.0f, 1.0f, 1.0f};
    Vector3 scaleRandom = {0.0f, 0.0f, 0.0f};

    Vector3 baseRotate = {0.0f, 0.0f, 0.0f};
    Vector3 rotateRandom = {0.0f, 0.0f, 0.0f};

    Vector3 scaleVelocity = {0.0f, 0.0f, 0.0f};
    bool isConverge = false;

    float weightMin = 1.0f;  // 重さの最小
    float weightMax = 1.0f;  // 重さの最大
    std::optional<Vector3> colorEnd; // 寿命の終わりの色。なければ color のまま変わらない
    float scaleEndRate = 1.0f; // 寿命の終わりの大きさの倍率
    float fadeStart = 0.0f;  // 薄くなり始める寿命の割合（0 で生まれた瞬間から薄くなる）

    // 箱の表面から出すときの設定（shape が BoxSurface のときだけ使う。速さは baseVelocity に足される）
    ParticleEmitShape shape = ParticleEmitShape::Volume;
    Matrix4x4 boxWorld = MakeIdentity4x4(); // 箱の中 → ワールド（回転と位置だけ）
    Vector3 boxHalfSize = {1.0f, 1.0f, 1.0f}; // 箱の半分の大きさ（m）
    Vector3 bandCenter = {0.0f, 0.0f, 0.0f}; // 距離を測る点
    float bandInner = 0.0f;  // 出す距離の範囲の内側（m）
    float bandOuter = 1.0e6f; // 出す距離の範囲の外側（m）
    float surfaceSpeed = 0.0f; // 面の外向きにはがれる速さ（m/秒）

    Vector3 rotateVelocityRandom = {0.0f, 0.0f, 0.0f}; // 回る速さのばらつき（ラジアン/秒。-1〜1 倍）
};

class IParticleEmitter {
public:
    // 描画時の合成方法（Add: 加算, Alpha: 半透明）
    // Premultiplied: 色は透明度を掛け済みで足し、背景は (1 - α) の割合で残す（足し算と半透明の中間に使う）
    enum class BlendMode { Add, Alpha, Premultiplied };

    IParticleEmitter() = default;
    virtual ~IParticleEmitter();

    void SetBlendMode(BlendMode mode) { blendMode_ = mode; }

    // 1 フレームに積める出す頼みの数
    static constexpr uint32_t kMaxEmitRequests = 256;
    // 粒の上限の初期値
    static constexpr uint32_t kDefaultMaxParticles = 1024;

    void SetUVTransform(const Matrix4x4& uvTransform) { materialData_->uvTransform = uvTransform; }
    void SetMaterialColor(const Vector4& color) { materialData_->color = color; }
    void SetIsRingMode(bool isRing) { materialData_->isRingMode = isRing ? 1 : 0; }

    // 炎の描き方（Particle.hlsli の kFireOff・kFireFlame・kFireBall と同じ値）
    enum class FireMode { Off = 0, Flame = 1, Ball = 2 };

    /// <summary>
    /// 炎の描き方にするか。絵を模様として使い、寿命が進むほど薄い所から削って、温度の色（白 → 黄 → 橙 → 赤）で塗る
    /// </summary>
    /// <param name="mode">Flame は上へ伸びる舌の形、Ball は丸いもやの形</param>
    void SetFireMode(FireMode mode) { materialData_->isFireMode = static_cast<int32_t>(mode); }

    /// <summary>
    /// 炎の描き方の模様を置く原点と、模様を流す時間
    /// </summary>
    /// <param name="origin">模様の原点（ワールド座標）。燃えている物と一緒に動かすと、模様もついていく</param>
    /// <param name="time">模様を流す時間（秒）</param>
    void SetFireNoise(const Vector3& origin, float time) {
        materialData_->fireNoiseOrigin = origin;
        materialData_->fireTime = time;
    }

    /// <summary>
    /// カメラへ向けた板の中で、粒の回転（rotate）もかけるか。かけないときは回転を無視して真正面を向く
    /// </summary>
    void SetBillboardSpin(bool enable) { materialData_->isBillboard = enable ? kBillboardSpin_ : kBillboardFlat_; }

    /// <summary>
    /// 粒を、画面の上で飛ぶ向きへ速さに合わせて伸ばした線にする。頭は粒の位置で、尾は飛んできた側へ伸びる
    /// 色は寿命につれて白 → 黄 → 橙 → 赤へ冷め、粒の色（rgb）は使わない
    /// </summary>
    /// <param name="seconds">線の長さを「画面に沿った速さ × この秒数」にする</param>
    /// <param name="widthRate">線の半分の太さを「粒の大きさ × この倍率」にする</param>
    void SetVelocityStreak(float seconds, float widthRate) {
        materialData_->isBillboard = kBillboardStreak_;
        materialData_->streakSeconds = seconds;
        materialData_->streakWidthRate = widthRate;
    }

    // 粒にかかる力（入れ物の全部の粒に共通）
    void SetGravity(const Vector3& gravity) { motionData_->gravity = gravity; }
    void SetWind(const Vector3& wind) { motionData_->wind = wind; }
    void SetDrag(float drag) { motionData_->drag = drag; }
    const ParticleMotion& GetMotion() const { return *motionData_; }

    ID3D12Resource* GetMotionResource() const { return motionResource_.Get(); }
    uint32_t GetMaxParticles() const { return maxParticles_; }

    /// <summary>
    /// このフレームに積まれた出す頼みを GPU が読むバッファへ写し、列を空にする
    /// </summary>
    /// <param name="maxCount">頼みの中で一番多い出す数</param>
    /// <returns>写した頼みの数</returns>
    uint32_t FlushEmitRequests(uint32_t& maxCount);
    ID3D12Resource* GetEmitRequestResource() const { return emitRequestResource_.Get(); }
    ID3D12Resource* GetGPUParticleResource() const { return gpuParticleResource_.Get(); }
    ID3D12Resource* GetFreeListIndexResource() const { return freeListIndexResource_.Get(); }
    ID3D12Resource* GetFreeListResource() const { return freeListResource_.Get(); }
    uint32_t GetGPUParticleUavIndex() const { return gpuParticleUav_.Index(); }

    virtual void Update(const Matrix4x4& viewMatrix, const Matrix4x4& projectionMatrix) = 0;
    virtual void Draw();

    bool needsClear_ = false;

    /// <summary>
    /// 生きている粒をすべて消し、まだ出していない発生の予約も取り消す（実際に消すのは次の ParticleManager::Emit）
    /// </summary>
    void ClearParticles();

    /// <summary>
    /// 出す頼みを列に積む（GPU で出すのは次の ParticleManager::Emit）
    /// </summary>
    void Emit(const ParticleEmitDesc& desc);
    void Emit(const Vector3& position, uint32_t count,
              const Vector3& baseVelocity = {0.0f, 0.0f, 0.0f},
              const Vector3& velocityRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& spawnRandom = {0.0f, 0.0f, 0.0f},
              float lifeMin = 1.0f, float lifeMax = 1.0f,
              const Vector4& color = {1.0f, 1.0f, 1.0f, 1.0f},
              const Vector3& baseScale = {1.0f, 1.0f, 1.0f},
              const Vector3& scaleRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& baseRotate = {0.0f, 0.0f, 0.0f},
              const Vector3& rotateRandom = {0.0f, 0.0f, 0.0f},
              const Vector3& scaleVelocity = {0.0f, 0.0f, 0.0f});

protected:
    /// <summary>
    /// 共通の初期化
    /// </summary>
    /// <param name="textureFilePath">粒の絵</param>
    /// <param name="maxParticles">同時に生きていられる粒の上限</param>
    void BaseInitialize(const std::string& textureFilePath, uint32_t maxParticles);
    void CreateInstancingResource();
    void CreateMaterialResource();

    // isBillboard の値（Particle.hlsli の同じ名前の値と同じ）
    static constexpr int32_t kBillboardFlat_ = 1; // カメラへ向け、粒の回転は無視する
    static constexpr int32_t kBillboardSpin_ = 2; // カメラへ向けた板の中で、粒の回転をかける
    static constexpr int32_t kBillboardStreak_ = 3; // 画面の上で、飛ぶ向きへ速さに合わせて伸ばした線にする（Particle.hlsli の kBillboardStreak と同じ）

    std::string textureFilePath_;
    uint32_t textureSrvIndex_ = 0;

    BlendMode blendMode_ = BlendMode::Add;

    // GPUパーティクル用のリソース群
    std::vector<EmitterData> emitRequests_; // このフレームに積まれた出す頼み
    Microsoft::WRL::ComPtr<ID3D12Resource> emitRequestResource_; // 出す頼みを GPU へ渡すバッファ（kMaxEmitRequests 件）
    EmitterData* emitRequestData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> motionResource_;
    ParticleMotion* motionData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> gpuParticleResource_;
    SrvHandle gpuParticleSrv_;
    SrvHandle gpuParticleUav_;

    Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
    
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    uint32_t vertexCount_ = 0;

    struct Material {
        Vector4 color;
        int32_t enableLighting;
        int32_t isBillboard;
        int32_t isRingMode;
        int32_t isFireMode;
        Matrix4x4 uvTransform;
        Vector3 fireNoiseOrigin;
        float fireTime;
        float streakSeconds;
        float streakWidthRate;
        float padding[2];
    };
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Material* materialData_ = nullptr;

    uint32_t maxParticles_ = kDefaultMaxParticles; // 同時に生きていられる粒の上限

    std::random_device seedGenerator_;
    std::mt19937 randomEngine_;
};
