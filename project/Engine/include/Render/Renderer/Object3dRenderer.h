#pragma once
#include "Core/Dx12Core.h"
#include "Math/MathUtil.h"
#include "Renderer/Fog.h"

class ICamera;
class LightManager;
class TiledLightCuller;

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

  /// <summary>
  /// Object3d を深度だけの列（P）にも積むか。
  /// 3 つすべて: Begin3DPass 〜 End3DPass の間 / 深度テストが有効 / Tiled Culling がこのフレームで動いている
  /// </summary>
  bool IsRecordingPrepass() const;

  /// <summary>
  /// P の命令の列を返す。このフレームで P に初めて積むときは、ルートシグネチャ・深度だけの PSO・トポロジを先に積む
  /// </summary>
  ID3D12GraphicsCommandList *PreparePrepassCommandList();

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
  // 深度だけの描画（色は出さず、深度だけの絵に書く）
  Microsoft::WRL::ComPtr<ID3D12PipelineState> depthPrepassPipelineState_ = nullptr;
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
    // 調べるための表示（0: 通常, 1: テクスチャの色だけ, 2: 法線, 3: 法線の向き, 4: 光だけ, 5: 壊れた法線, 6: マスごとのライトの数）
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
  /// <summary>
  /// 点の光・スポットライトの名簿を渡す（Object3d が描くときに使う）
  /// </summary>
  void SetLightManager(LightManager *lightManager) { lightManager_ = lightManager; }
  LightManager *GetLightManager() const { return lightManager_; }

private:
  LightManager *lightManager_ = nullptr;

public:
  /// <summary>
  /// マス分け（深度だけの絵とマスごとの名簿）を渡す
  /// </summary>
  void SetTiledLightCuller(TiledLightCuller *tiledLightCuller) { tiledLightCuller_ = tiledLightCuller; }
  TiledLightCuller *GetTiledLightCuller() const { return tiledLightCuller_; }

private:
  TiledLightCuller *tiledLightCuller_ = nullptr;

  // 今 B に積んでいる Object3d の PSO が深度テストありか（SetDepthEnable で切り替わる）
  bool depthEnabled_ = true;

  // P にルートシグネチャ・PSO・トポロジを積んだ回の番号（TiledLightCuller の GetPrepassFrame と同じなら積み済み）
  uint64_t prepassStateFrame_ = 0;

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
