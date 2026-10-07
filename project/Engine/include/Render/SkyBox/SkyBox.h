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
    // x: モード(0=キューブマップ, 1=手続き空), y: 時間(雲スクロール用), z,w: 未使用
    Vector4 skyParams;
    Vector4 zenithColor;  // 天頂の色
    Vector4 horizonColor; // 地平線の色
    Vector4 sunDir;       // xyz: 太陽の方向(正規化), w: 雲の量(0〜1)
    Vector4 sunColor;     // rgb: 太陽の色, a: 太陽の強さ
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