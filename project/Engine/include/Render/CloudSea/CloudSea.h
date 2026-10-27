#pragma once
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include <cstdint>
#include <wrl.h>

class ICamera;
class CloudSeaRenderer;

/// <summary>
/// 雲海の形を変える指示（盛り上がり・裂け目）。毎フレーム設定し直す
/// </summary>
struct CloudSeaShape {
  enum class Kind : int32_t {
    Swell = 0, // 盛り上がり
    Tear = 1,  // 裂け目（内側の雲を無くす）
  };
  Kind kind = Kind::Swell;
  Vector2 center = {0.0f, 0.0f}; // ワールドの XZ
  float radius = 0.0f; // 盛り上がり: 広がりの半径、裂け目: 穴の最大の半径
  float width = 0.0f;  // 裂け目: 縁のめくれの高さ
  float amount = 0.0f; // 盛り上がり: 高さ、裂け目: 穴の開き具合（0〜1）
  float crack = 0.0f;  // 盛り上がり: 泡立ちの強さ（0〜1）
};

/// <summary>
/// 雲の中の 1 点から照らす光（雷など）。毎フレーム設定し直す
/// </summary>
struct CloudSeaGlow {
  Vector3 position = {0.0f, 0.0f, 0.0f}; // ワールドの位置
  float radius = 1.0f; // 光が届く広がり（この距離で 1/e に弱まる）
  Vector3 color = {0.0f, 0.0f, 0.0f}; // 光の色と強さ（雲の色によらず足す）
};

/// <summary>
/// 雲海の見た目の値
/// </summary>
struct CloudSeaLook {
  float thickness = 25.0f;   // 層の厚み（上の面の基準の高さから下へ）
  float edgeSoftness = 0.8f; // 上の面から濃さが 1 になるまでの深さ
  float density = 1.6f;      // 濃さ 1 の所で、長さ 1 あたりに光が弱まる割合
  Vector3 color = {0.16f, 0.16f, 0.17f}; // 雲の色（光を受けて返す割合）
  float brightness = 1.0f;     // 雲が返す光の強さの倍率
  float powder = 1.0f;         // 粉の暗さの強さ（0〜1）
  float valleyDarkness = 0.3f; // 低い所の環境光を暗くする強さ（0〜1）
  float forwardScatter = 0.0f; // 光の進む向きへ強く散る度合いの混ぜ具合（0〜1）
  float rimStrength = 0.0f;    // 地平線の光が上の縁を透ける強さ（0 で無効）
  float rimPower = 3.0f;       // 地平線の光の縁の鋭さ
  float noiseSize = 40.0f;     // 大きなノイズ 1 枚のワールドの長さ
  float detailSize = 6.0f;     // 細かいノイズ 1 枚のワールドの長さ
  float detailStrength = 0.35f; // 細かいノイズで縁を削る強さ（0〜0.9）
  float jitter = 1.0f; // 視線の出発点を画素ごとにずらす量（0〜1）
};

/// <summary>
/// 雲海。中身の詰まった雲の層を、画面の点ごとに視線を進めて濃さと光を足していく描き方（レイマーチ）で描く。
/// 層の上の面は起伏のある高さの式で決め、縁を 3 次元のノイズで削る。
/// 不透明な物を描いたあとに、深度を読んで物の手前までの雲を半分の解像度で描き、元の大きさの HDR の画面へ重ねる
/// </summary>
class CloudSea {
public:
  static constexpr int kMaxShapes = 8;
  static constexpr int kMaxGlows = 4;

  // 調べるための表示
  enum class DebugView : int32_t {
    Normal = 0,     // 通常
    SunOnly = 1,    // 太陽の光だけ
    AmbientOnly = 2, // 環境光だけ
    DensityOnly = 3, // 濃さだけ（光を一様な白にする）
    StepCount = 4,   // 濃さを測った回数（上限で打ち切った所は赤）
  };

  void Initialize(CloudSeaRenderer *renderer);
  void Update();
  /// <summary>
  /// 不透明な物を描いたあとに呼ぶ（深度を読むため、描画先の設定を一度切り替えて戻す）
  /// </summary>
  void Draw();

  void SetCamera(const ICamera *camera) { camera_ = camera; }

  /// <summary>
  /// 形の指示を全部消す
  /// </summary>
  void ClearShapes();
  /// <summary>
  /// index 番目の形の指示を設定する（0 〜 kMaxShapes - 1）
  /// </summary>
  void SetShape(int index, const CloudSeaShape &shape);

  /// <summary>
  /// 雲の中から照らす光を全部消す
  /// </summary>
  void ClearGlows();
  /// <summary>
  /// index 番目の雲の中から照らす光を設定する（0 〜 kMaxGlows - 1）
  /// </summary>
  void SetGlow(int index, const CloudSeaGlow &glow);

  // 雲の層の上の面の基準の高さ（ワールドの Y。起伏はこの上下に付く）
  void SetHeight(float height) { height_ = height; }
  float GetHeight() const { return height_; }
  /// <summary>
  /// 上の面の起伏の高さ（基準から上下へ）と、雲の塊の大きさ（ワールドの長さ）
  /// </summary>
  void SetRelief(float amplitude, float blobSize) {
    amplitude_ = amplitude;
    blobSize_ = blobSize;
  }
  // 風で雲が流れる速さ（ワールドの XZ、1 秒あたり）
  void SetWind(const Vector2 &wind) { wind_ = wind; }
  void SetLook(const CloudSeaLook &look) { look_ = look; }
  void SetDebugView(DebugView view) { debugView_ = view; }
  // 風で流す時刻（秒）
  void SetTime(float time) { time_ = time; }

private:
  // 形の指示（シェーダーの CloudSeaShape と同じ並び。32 バイト）
  struct ShapeForGPU {
    Vector2 center;
    float radius;
    float width;
    float amount;
    float crack;
    int32_t kind;
    float padding;
  };

  // 雲の中から照らす光（シェーダーの CloudSeaGlow と同じ並び。32 バイト）
  struct GlowForGPU {
    Vector3 position;
    float radius;
    Vector3 color;
    float padding;
  };

  // 雲海の値（シェーダーの CloudSeaParams と同じ並び）
  struct ParamsForGPU {
    Matrix4x4 inverseViewProjection;
    Vector3 cameraPosition;
    float time;
    float height;
    float amplitude;
    float blobSize;
    float thickness;
    Vector2 wind;
    float edgeSoftness;
    float density;
    Vector4 color; // rgb: 雲の色
    float brightness;
    float powder;
    float valleyDarkness;
    float forwardScatter;
    float rimStrength;
    float rimPower;
    float noiseSize;
    float detailSize;
    float detailStrength;
    float jitter;
    int32_t debugView;
    int32_t shapeCount;
    Vector2 screenSize;
    float projection22;
    float projection32;
    ShapeForGPU shapes[kMaxShapes];
    GlowForGPU glows[kMaxGlows];
    int32_t glowCount;
    float glowPadding[3];
  };

  void CreateParamsResource();

private:
  CloudSeaRenderer *renderer_ = nullptr;
  Dx12Core *dx12Core_ = nullptr;
  const ICamera *camera_ = nullptr;

  float height_ = -15.0f;
  float amplitude_ = 3.0f;
  float blobSize_ = 30.0f;
  Vector2 wind_ = {0.0f, 0.0f};
  CloudSeaLook look_;
  DebugView debugView_ = DebugView::Normal;
  float time_ = 0.0f;
  CloudSeaShape shapes_[kMaxShapes] = {};
  int shapeCount_ = 0;
  CloudSeaGlow glows_[kMaxGlows] = {};
  int glowCount_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
  ParamsForGPU *paramsData_ = nullptr;
};
