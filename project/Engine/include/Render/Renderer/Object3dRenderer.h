#pragma once
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include "Renderer/Fog.h"

class ICamera;

class Object3dRenderer {

public:
  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="dx12Core"></param>
  void Initialize(Dx12Core *dx12Core);

  /// <summary>
  /// 描画前共通部分処理
  /// </summary>
  void Begin();

  /// <summary>
  /// デプステストの有効/無効を切り替える
  /// </summary>
  void SetDepthEnable(bool enable);

private:
  Dx12Core *dx12Core_ = nullptr;

public:
  Dx12Core *GetDx12Core() const { return dx12Core_; }

private:
  // ルートシグネチャ
  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> skinningComputeRootSignature_ = nullptr;

  // PSO
  Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipeLineState_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipeLineStateDepthDisabled_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> skinningComputePipelineState_ = nullptr;

public:
  ID3D12RootSignature* GetSkinningComputeRootSignature() const { return skinningComputeRootSignature_.Get(); }
  ID3D12PipelineState* GetSkinningComputePipelineState() const { return skinningComputePipelineState_.Get(); }
  /// <summary>
  /// ルートシグネチャを作成
  /// </summary>
  void CreateRootSignature();
  void CreateSkinningComputeRootSignature();

  /// <summary>
  /// オブジェクト用のPSOを作成
  /// </summary>
  void CreatePSO();
  void CreateSkinningComputePSO();

private:
  const ICamera *defaultCamera = nullptr;

public:
  void SetDefaultCamera(const ICamera *camera) { defaultCamera = camera; }
  const ICamera *GetDefaultCamera() const { return defaultCamera; }

public:
  struct DirectionalLight {
    Vector4 color;     // ライトの色
    Vector3 direction; // ライトの向き
    float intensity;   // 輝度
    // 環境光（法線が上を向くほど skyAmbient、下を向くほど groundAmbient）。rgb のみ使う
    Vector4 skyAmbient;
    Vector4 groundAmbient;
    // 逆光の縁: rgb=色, w=強度(0で無効)。マテリアルの backRimScale を掛けて使う
    Vector4 backRimColor;
    float backRimPower;       // 大きいほど縁の細い範囲だけ光る
    Vector3 backRimDirection; // 逆光の縁を作る光が進む向き
    // 調べるための表示（0: 通常, 1: テクスチャの色だけ, 2: 法線, 3: 法線の向き, 4: 光だけ, 5: 壊れた法線）
    int32_t debugView;
    float padding[3];
  };

  ID3D12Resource *GetDirectionalLightResource() {
    return directionalLightResource_.Get();
  }

  DirectionalLight *GetDirectionalLightData() { return directionalLightData_; }

private:
  /* 平行光源データ
 -----------------------------*/
  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_ = nullptr;

  // バッファリソース内のデータを指すポインタ
  DirectionalLight *directionalLightData_ = nullptr;

  /// <summary>
  /// 平行光源データを生成
  /// </summary>
  void CreateDirectionalLightData();

public:
  struct PointLight {
    Vector4 color;    // 色
    Vector3 position; // 位置
    float intensity;  // 輝度
    float radius;     // ライトの届く最大距離
    float decay;      // 減衰率
    float padding[2];
  };

  ID3D12Resource *GetPointLightResource() const {
    return pointLightResource_.Get();
  }

  PointLight *GetPointLightData() { return pointLightData_; }

private:
  /* PointLight用データ
　-----------------------------*/
  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> pointLightResource_ = nullptr;

  // バッファリソース内のデータを指すポインタ
  Object3dRenderer::PointLight *pointLightData_ = nullptr;

  /// <summary>
  /// PointLightを生成
  /// </summary>
  void CreatePointLightData();

public:
  struct SpotLight {
    Vector4 color;
    Vector3 position;
    float intensity;
    Vector3 direction;
    float distance;
    float decay;
    float cosAngle; // スポットライトの余弦
    float padding[2];
  };

  ID3D12Resource *GetSpotLightResource() const {
    return spotLightResource_.Get();
  }

  SpotLight *GetSpotLightData() { return spotLightData_; }

private:
  /* SpotLight用データ
　-----------------------------*/
  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> spotLightResource_ = nullptr;

  // バッファリソース内のデータを指すポインタ
  Object3dRenderer::SpotLight *spotLightData_ = nullptr;

  /// <summary>
  /// SpotLightを生成
  /// </summary>
  void CreateSpotLightData();

public:
  // --- フォグ関連 ---
  ID3D12Resource *GetFogResource() const { return fogResource_.Get(); }
  FogData *GetFogData() { return fogData_; }

  /// <summary>
  /// フォグのパラメータを一括設定
  /// </summary>
  void SetFog(const FogData& fog) {
    if (fogData_) {
      *fogData_ = fog;
    }
  }

private:
  Microsoft::WRL::ComPtr<ID3D12Resource> fogResource_ = nullptr;
  FogData *fogData_ = nullptr;

  void CreateFogData();
};
