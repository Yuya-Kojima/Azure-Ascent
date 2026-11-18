#pragma once
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include "Math/VertexData.h"
#include <string>
#include <vector>
#include <wrl.h>

class ICamera;
class SkyboxRenderer;

class Skybox {

  struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
  };

  struct Material {
    Vector4 color;
    // x: モード(0=キューブマップ, 1=手続き空), y: 時間(雲スクロール用), z: 雲の縁の明るさ, w: 未使用
    Vector4 skyParams;
    Vector4 zenithColor;  // 天頂の色
    Vector4 horizonColor; // 地平線の色
    Vector4 sunDir;       // xyz: 太陽の方向(正規化), w: 雲の量(0〜1)
    Vector4 sunColor;     // rgb: 太陽の色, a: 太陽の強さ
    Vector4 cloudColor;   // rgb: 雲の色, a: 雲の不透明度（0 のときは白い雲）
    // x: 地平線の光の強さ(0で無効), y: 光の帯の高さ, z: 方位の絞り, w: 未使用
    Vector4 horizonGlow;

    // 晴れた空の値（円の内側で使う。並びは上の空の値と同じ）
    Vector4 clearZenithColor;  // 天頂の色
    Vector4 clearHorizonColor; // 地平線の色
    Vector4 clearSunDir;       // xyz: 太陽の方向(正規化), w: 雲の量(0〜1)
    Vector4 clearSunColor;     // rgb: 太陽の色, a: 太陽の強さ
    Vector4 clearCloudColor;   // rgb: 雲の色, a: 雲の不透明度（0 のときは白い雲）
    // x: 地平線の光の強さ, y: 光の帯の高さ, z: 方位の絞り, w: 雲の縁の明るさ
    Vector4 clearHorizonGlow;
    // xyz: 晴れの円の中心の向き(正規化), w: 円の半径（中心からの角度、ラジアン。0 以下で晴れなし）
    Vector4 clearCircle;
    // x: 境目のぼかしの幅, y: 境目の外の闇が濃くなる帯の幅, z: 闇が濃くなる強さ(0〜1), w: 境目をノイズで揺らす幅（角度はすべてラジアン）
    Vector4 clearEdge;
  };

public:
  void Initialize(SkyboxRenderer *skyboxRenderer);
  void Update();
  void Draw();

  void SetCamera(const ICamera *camera) { camera_ = camera; }
  void SetTexture(const std::string &filePath) { textureFilePath_ = filePath; }

  void SetScale(const Vector3 &scale) { transform_.scale = scale; }
  void SetRotation(const Vector3 &rotate) { transform_.rotate = rotate; }
  void SetTranslation(const Vector3 &translate) {
    transform_.translate = translate;
  }
  void SetColor(const Vector4 &color) { materialData_->color = color; }

  /// 手続き空モードの有効/無効（無効時は従来のキューブマップ描画）
  void SetProceduralSky(bool enable) {
    materialData_->skyParams.x = enable ? 1.0f : 0.0f;
  }
  /// 手続き空の確認用の表示（0: ふつう, 1: 向きを色で, 2: 雲のノイズを白黒で, 3: 晴れの割合を白黒で）
  void SetDebugView(int view) {
    materialData_->skyParams.w = static_cast<float>(view);
  }
  /// 手続き空のパラメータ設定
  void SetSkyColors(const Vector3 &zenith, const Vector3 &horizon) {
    materialData_->zenithColor = {zenith.x, zenith.y, zenith.z, 1.0f};
    materialData_->horizonColor = {horizon.x, horizon.y, horizon.z, 1.0f};
  }
  void SetSun(const Vector3 &dir, const Vector3 &color, float intensity,
              float cloudAmount) {
    materialData_->sunDir = {dir.x, dir.y, dir.z, cloudAmount};
    materialData_->sunColor = {color.x, color.y, color.z, intensity};
  }
  void SetSkyTime(float time) { materialData_->skyParams.y = time; }
  /// 雲の色を指定する（opacity が 0 のときは白い雲）。rim は太陽側で雲の縁が光る強さ
  void SetCloudColor(const Vector3 &color, float opacity, float rim) {
    materialData_->cloudColor = {color.x, color.y, color.z, opacity};
    materialData_->skyParams.z = rim;
  }
  /// 太陽の方位で地平線のすぐ上を光らせる。height は帯の高さ（方向ベクトルの y）、
  /// azimuthPower が大きいほど太陽の左右の狭い範囲だけ光る。strength=0で無効
  void SetHorizonGlow(float strength, float height, float azimuthPower) {
    materialData_->horizonGlow = {strength, height, azimuthPower, 0.0f};
  }

  /// 晴れた空の値（SetSkyColors〜SetHorizonGlow と同じ意味。晴れの円の内側で使う）
  void SetClearSkyColors(const Vector3 &zenith, const Vector3 &horizon) {
    materialData_->clearZenithColor = {zenith.x, zenith.y, zenith.z, 1.0f};
    materialData_->clearHorizonColor = {horizon.x, horizon.y, horizon.z, 1.0f};
  }
  void SetClearSun(const Vector3 &dir, const Vector3 &color, float intensity,
                   float cloudAmount) {
    materialData_->clearSunDir = {dir.x, dir.y, dir.z, cloudAmount};
    materialData_->clearSunColor = {color.x, color.y, color.z, intensity};
  }
  void SetClearCloudColor(const Vector3 &color, float opacity, float rim) {
    materialData_->clearCloudColor = {color.x, color.y, color.z, opacity};
    materialData_->clearHorizonGlow.w = rim;
  }
  void SetClearHorizonGlow(float strength, float height, float azimuthPower) {
    materialData_->clearHorizonGlow.x = strength;
    materialData_->clearHorizonGlow.y = height;
    materialData_->clearHorizonGlow.z = azimuthPower;
  }
  /// 晴れの円。center の向きから radius（ラジアン）より内側を晴れた空で描く。radius が 0 以下なら晴れなし
  void SetClearCircle(const Vector3 &center, float radius) {
    materialData_->clearCircle = {center.x, center.y, center.z, radius};
  }
  /// 晴れの円の境目。softness はぼかしの幅、darkWidth は外側で闇が濃くなる帯の幅、
  /// darkStrength はその濃くなる強さ（0〜1）、noise は境目をノイズで揺らす幅（角度はラジアン）
  void SetClearEdge(float softness, float darkWidth, float darkStrength,
                    float noise) {
    materialData_->clearEdge = {softness, darkWidth, darkStrength, noise};
  }

  Vector3 GetScale() const { return transform_.scale; }
  Vector3 GetRotation() const { return transform_.rotate; }
  Vector3 GetTranslation() const { return transform_.translate; }

private:
  void CreateVertices();
  void CreateVertexResource();
  void CreateTransformationMatrixResource();
  void CreateMaterialResource();

private:
  SkyboxRenderer *skyboxRenderer_ = nullptr;
  Dx12Core *dx12Core_ = nullptr;

  const ICamera *camera_ = nullptr;

  Transform transform_{
      {1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  std::string textureFilePath_;

  std::vector<VertexData> vertices_;

  Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
  VertexData *vertexData_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
  TransformationMatrix *transformationMatrixData_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
  Material *materialData_ = nullptr;
};