#pragma once
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include <cstdint>
#include <string>
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
  float crack = 0.0f;  // 盛り上がり: ひびの濃さ（0〜1）
};

/// <summary>
/// 雲海。カメラの真下を中心にした高さの網を頂点シェーダーで持ち上げて、起伏のある雲の面を描く。
/// 網は中心から外へ段ごとに目の幅を倍にし、段の外側の頂点を外の段の目に寄せて継ぎ目をなくす
/// </summary>
class CloudSea {
public:
  static constexpr int kMaxShapes = 8;

  void Initialize(CloudSeaRenderer *renderer, const std::string &texturePath);
  void Update();
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

  // 雲海の面の高さ（ワールドの Y。起伏はこの上下に付く）
  void SetHeight(float height) { height_ = height; }
  float GetHeight() const { return height_; }
  /// <summary>
  /// 起伏の高さ（面から上下へ）と、雲の塊の大きさ（ワールドの長さ）
  /// </summary>
  void SetRelief(float amplitude, float blobSize) {
    amplitude_ = amplitude;
    blobSize_ = blobSize;
  }
  // 風で模様が流れる速さ（ワールドの XZ、1 秒あたり）
  void SetWind(const Vector2 &wind) { wind_ = wind; }
  // 谷を暗くする強さ（0: 暗くしない 〜 1: 一番低い所が真っ黒）
  void SetValleyDarkness(float darkness) { valleyDarkness_ = darkness; }
  /// <summary>
  /// 逆光の縁（地平線の光を受けて塊の縁が光る）の強さと鋭さ。強さ 0 で無効
  /// </summary>
  void SetRim(float strength, float power) {
    rimStrength_ = strength;
    rimPower_ = power;
  }
  void SetColor(const Vector3 &color) { color_ = color; }
  // 模様のテクスチャを 1 枚貼る長さの逆数（ワールドの長さ 1 あたりの繰り返し回数）
  void SetTextureScale(float scale) { textureScale_ = scale; }
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

  // 雲海の値（シェーダーの CloudSeaParams と同じ並び）
  struct ParamsForGPU {
    Matrix4x4 viewProjection;
    Vector3 cameraPosition;
    float time;
    Vector2 gridOrigin; // 網の中心（ワールドの XZ）
    float height;
    float amplitude;
    Vector2 wind;
    float blobSize;
    float valleyDarkness;
    Vector4 color; // rgb: 雲海の色
    float rimStrength;
    float rimPower;
    float textureScale;
    int32_t shapeCount;
    ShapeForGPU shapes[kMaxShapes];
  };

  void CreateGrid();
  void CreateParamsResource();

private:
  // 一番細かい段の目の幅（ワールドの長さ）
  static constexpr float kFinestSpacing_ = 1.0f;
  // 1 段の中心から外の縁までの目の数（偶数）
  static constexpr int kHalfCells_ = 64;
  // 段の数（目の幅は段ごとに倍。外の縁は kHalfCells_ × 一番粗い目の幅）
  static constexpr int kLevelCount_ = 6;
  // 段の外の縁から内側へ、外の段の目に寄せていく目の数
  static constexpr int kMorphCells_ = 16;

  CloudSeaRenderer *renderer_ = nullptr;
  Dx12Core *dx12Core_ = nullptr;
  const ICamera *camera_ = nullptr;
  std::string texturePath_;

  float height_ = -15.0f;
  float amplitude_ = 3.0f;
  float blobSize_ = 30.0f;
  Vector2 wind_ = {0.0f, 0.0f};
  float valleyDarkness_ = 0.6f;
  float rimStrength_ = 0.0f;
  float rimPower_ = 3.0f;
  Vector3 color_ = {0.16f, 0.16f, 0.17f};
  float textureScale_ = 0.01f;
  float time_ = 0.0f;
  CloudSeaShape shapes_[kMaxShapes] = {};
  int shapeCount_ = 0;

  UINT indexCount_ = 0;
  Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
  Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
  D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

  Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
  ParamsForGPU *paramsData_ = nullptr;
};
