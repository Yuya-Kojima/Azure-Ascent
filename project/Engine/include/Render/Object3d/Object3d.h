#pragma once
#include "Camera/CameraForGPU.h"
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include "Model/Model.h"
#include "Framework/BaseActor.h"
#include <d3d12.h>
#include <stdint.h>
#include <string>
#include <wrl.h>

class Dx12Core;

class Object3dRenderer;

class ICamera;
struct SkinCluster;

class Object3d {

  struct TransformationMatrix {
    Matrix4x4 WVP;
    Matrix4x4 World;
    Matrix4x4 WorldInverseTranspose;
  };

public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(Object3dRenderer *object3dRenderer);

  /// <summary>
  /// 更新
  /// </summary>
  void Update();

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

private:
  Object3dRenderer *object3dRenderer_ = nullptr;

  Dx12Core *dx12Core_ = nullptr;

  Model *model_ = nullptr;
  SkinCluster *skinCluster_ = nullptr;

public:
  Model *GetModel() const { return model_; }

  void SetModel(const std::string &filePath);

  void SetModel(Model *model) { model_ = model; }
  
  const std::string& GetModelPath() const { return modelFilePath_; }

  void SetSkinCluster(SkinCluster *skinCluster) { skinCluster_ = skinCluster; }

  std::string name_ = "Object3d";
  ActorTag tag_ = ActorTag::Untagged;

private:
  /* 座標変換行列データ
  -----------------------------*/
  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  TransformationMatrix *transformationMatrixData = nullptr;

  /// <summary>
  /// 座標変換行列データを生成
  /// </summary>
  void CreateTransformationMatrixData();

  /* 鏡面反射用データ
  -----------------------------*/
  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> cameraForGPUResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  CameraForGPU *cameraForGPUData = nullptr;

  /// <summary>
  /// カメラデータを生成
  /// </summary>
  void CreateCameraForGPUData();

  /* マテリアルデータ
----------------------------*/

  // マテリアルバッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  Model::Material *materialData = nullptr;

  /// <summary>
  /// マテリアルデータを作成
  /// </summary>
  void CreateMaterialData();

private:
  Transform transform_{
      {1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  Animation currentAnimation_;
  float animationTime_ = 0.0f;
  bool isPlayingAnimation_ = false;

public:
  void PlayAnimation(const Animation& animation) {
      currentAnimation_ = animation;
      animationTime_ = 0.0f;
      isPlayingAnimation_ = true;
  }

  // scale
  Vector3 GetScale() const { return transform_.scale; }

  void SetScale(const Vector3 &scale) { transform_.scale = scale; }

  // rotation
  Vector3 GetRotation() const { return transform_.rotate; }

  void SetRotation(const Vector3 &rotate) { transform_.rotate = rotate; }

  // translation
  Vector3 GetTranslation() const { return transform_.translate; }

  void SetTranslation(const Vector3 &translate) {
    transform_.translate = translate;
  }

  Vector4 GetColor() const {
    return materialData ? materialData->color : Vector4(1, 1, 1, 1);
  }

  void SetColor(const Vector4 &color) {
    if (materialData) {
      materialData->color = color;
    }
  }

  void SetAlpha(float a) {
    if (materialData) {
      materialData->color.w = a;
    }
  }

  bool GetEnableLighting() const {
    return materialData ? (materialData->enableLighting != 0) : true;
  }

  void SetEnableLighting(bool enable) {
    if (materialData) {
      materialData->enableLighting = enable ? 1 : 0;
    }
  }

  float GetEnvironmentCoefficient() const {
    return materialData ? materialData->environmentCoefficient : 0.0f;
  }

  void SetEnvironmentCoefficient(float coefficient) {
    if (materialData) {
      materialData->environmentCoefficient = coefficient;
    }
  }

  /// <summary>
  /// 鏡面反射の鋭さ（大きいほどハイライトが小さくなり、実質消える）
  /// </summary>
  void SetShininess(float shininess) {
    if (materialData) {
      materialData->shininess = shininess;
    }
  }

  /// <summary>
  /// リムライト（縁の発光）の設定。intensity=0で無効
  /// </summary>
  void SetRimLight(const Vector3 &color, float intensity, float power = 3.0f) {
    if (materialData) {
      materialData->rimColor = {color.x, color.y, color.z, intensity};
      materialData->rimPower = power;
    }
  }

  /// <summary>
  /// ディゾルブエフェクトの有効/無効を切り替える
  /// </summary>
  /// <param name="enable">trueで有効、falseで無効</param>
  void SetEnableDissolve(bool enable) {
    if (materialData) {
      materialData->enableDissolve = enable ? 1 : 0;
    }
  }

  /// <summary>
  /// ディゾルブの進行度を設定する
  /// </summary>
  /// <param name="threshold">0.0f（完全表示）～ 1.0f（完全消失）</param>
  void SetDissolveThreshold(float threshold) {
    if (materialData) {
      materialData->dissolveThreshold = threshold;
    }
  }

  /// <summary>
  /// ディゾルブのエッジの太さを設定する
  /// </summary>
  /// <param name="range">エッジの太さ（例: 0.05f）</param>
  void SetDissolveEdgeRange(float range) {
    if (materialData) {
      materialData->dissolveEdgeRange = range;
    }
  }

  /// <summary>
  /// ディゾルブのエッジの色を設定する
  /// </summary>
  /// <param name="color">RGBAカラー</param>
  void SetDissolveEdgeColor(const Vector4& color) {
    if (materialData) {
      materialData->dissolveEdgeColor = color;
    }
  }

  /// <summary>
  /// ディゾルブに使用するマスク画像のファイルパスを設定する
  /// </summary>
  /// <param name="path">画像ファイルパス</param>
  void SetMaskTexturePath(const std::string& path);

  /// <summary>
  /// マスク画像のUVスクロール量を設定する
  /// </summary>
  /// <param name="transform">UVの移動量（X, Y）</param>
  void SetMaskTransform(const Vector2& transform) {
    if (materialData) {
      materialData->maskTransform = transform;
    }
  }

private:
  std::string maskTexturePath_ = "resources/noise0.png";
  std::string modelFilePath_ = "";

private:
  const ICamera *camera_ = nullptr;

  const Object3d *parent_ = nullptr;
  Matrix4x4 worldMatrix_{};

public:
  void SetCamera(const ICamera *camera) { this->camera_ = camera; }

  void SetParent(const Object3d *parent) { parent_ = parent; }
  const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
};
