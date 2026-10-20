#pragma once
#include "Core/WindowSystem.h"
#include "Math/Vector4.h"
#include "DirectXTex.h"
#include <array>
#include <chrono>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <string>
#include <wrl.h>
#include <unordered_map>

Microsoft::WRL::ComPtr<ID3D12Resource>
CreateRenderTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, uint32_t width, uint32_t height, DXGI_FORMAT format, const Vector4& clearColor);

class Dx12Core {
private:
  // DirectX12デバイス
  Microsoft::WRL::ComPtr<ID3D12Device> device;

  // DXGIファクトリー
  Microsoft::WRL::ComPtr<IDXGIFactory7> dxgiFactory;

  // コマンドアロケータ
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;

  // コマンドキュー
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue;

  // コマンドリスト
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;

  // WindowAPI
  WindowSystem *windowSystem = nullptr;

  // スワップチェーン
  Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain = nullptr;

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};

  // 深度バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = nullptr;

  // RTV用ヒープ
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
  uint32_t descriptorSizeRTV;

  // SRV用ヒープ
  // Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
  // uint32_t descriptorSizeSRV;

  // DSV用ヒープ
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap;
  uint32_t descriptorSizeDSV;

  // スワップチェーンからRTVに引っ張ってくるリソース(バックバッファ)
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> swapChainResources;

  // 取得したRTVハンドル
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};

  // 動的なRTV割り当て用
  uint32_t useRtvIndex_ = 0;

  // フェンスカウント
  uint64_t fenceValue;

  // フェンス
  Microsoft::WRL::ComPtr<ID3D12Fence> fence = nullptr;

  // フェンスイベント
  HANDLE fenceEvent;

  // ビューポート
  D3D12_VIEWPORT viewport{};

  // シザー矩形
  D3D12_RECT scissorRect{};

  // dxcユーティリティ
  Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils = nullptr;

  // dxcコンパイラ
  Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler = nullptr;

  // インクルードハンドラ
  Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler = nullptr;

  // リソースバリア
  D3D12_RESOURCE_BARRIER barrier{};

  /// <summary>
  /// CPUHandleの取得
  /// </summary>
  /// <param name="descriptorHeap">DescriptorHandle</param>
  /// <param name="descriptorSize">DescriptorSize</param>
  /// <param name="index">index</param>
  /// <returns></returns>
  static D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(
      Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &descriptorHeap,
      uint32_t descriptorSize, uint32_t index);

  /// <summary>
  /// GPUHandleの取得
  /// </summary>
  /// <param name="descriptorHeap">descriptorHeap</param>
  /// <param name="descriptorSize">descriptorSize</param>
  /// <param name="index">index</param>
  /// <returns></returns>
  static D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(
      Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &descriptorHeap,
      uint32_t descriptorSize, uint32_t index);

public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(WindowSystem *windowSystem);

  /// <summary>
  /// 描画前処理
  /// </summary>
  void BeginFrame();

  /// <summary>
  /// ImGui描画前処理(Swapchainへの切り替え)
  /// </summary>
  void PreDrawImGui();

  /// <summary>
  /// 描画後処理
  /// </summary>
  void EndFrame();

  /// <summary>
  /// リソース状態の安全な遷移（自動バリア）
  /// </summary>
  void TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState);

  /// <summary>
  /// 3D を描く先（HDR の画面）の RTV を覚える。3D の描画を始めるときに設定する
  /// </summary>
  void SetSceneRenderTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle) {
    sceneRtvHandle_ = rtvHandle;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE GetSceneRenderTarget() const {
    return sceneRtvHandle_;
  }

  /// <summary>
  /// 3D の途中で深度をシェーダーから読める状態にする（描画先は 3D の画面だけにする）
  /// </summary>
  void BeginSceneDepthRead();

  /// <summary>
  /// 深度を書き込める状態に戻し、描画先を 3D の画面と深度に戻す
  /// </summary>
  void EndSceneDepthRead();

  /// <summary>
  /// リソースの初期状態を名簿に手動登録する
  /// </summary>
  void RegisterResourceState(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState) {
      if (resource) {
          resourceStates_[resource] = initialState;
      }
  }

  /// <summary>
  /// デバイスの初期化
  /// </summary>
  void InitializeDevice();

  /// <summary>
  /// コマンド関連の初期化
  /// </summary>
  void InitializeCommand();

  /// <summary>
  /// スワップチェーンの生成
  /// </summary>
  void InitializeSwapChain();

  /// <summary>
  /// 深度バッファの生成
  /// </summary>
  void InitializeDepthBuffer();

  /// <summary>
  /// ディスクリプタヒープの生成
  /// </summary>
  void InitializeDescriptorHeap();

  /// <summary>
  /// ディスクリプタヒープ生成関数
  /// </summary>
  /// <param name="heapType"></param>
  /// <param name="numDescriptors"></param>
  /// <param name="shaderVisible"></param>
  /// <returns></returns>
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
  CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors,
                       bool shaderVisible);

  /// <summary>
  /// RTVの初期化
  /// </summary>
  void InitializeRenderTargetView();

  // D3D12_CPU_DESCRIPTOR_HANDLE GetSRVCPUDescriptorHandle(uint32_t index);

  // D3D12_GPU_DESCRIPTOR_HANDLE GetSRVGPUDescriptorHandle(uint32_t index);

  /// <summary>
  /// DSVの初期化
  /// </summary>
  void InitializeDepthStencilView();

  /// <summary>
  /// フェンスの初期化
  /// </summary>
  void InitializeFence();

  /// <summary>
  /// ビューポート矩形の初期化
  /// </summary>
  void InitializeViewport();

  /// <summary>
  /// シザー矩形の初期化
  /// </summary>
  void InitializeScissorRect();

  /// <summary>
  /// DXCコンパイラの初期化
  /// </summary>
  void InitializeDXCcompiler();

  /// <summary>
  /// ImGuiの初期化
  /// </summary>
  void InitializeImGui();

  /// <summary>
  /// RTVインデックスの割り当て
  /// </summary>
  uint32_t AllocateRTV() { return useRtvIndex_++; }

  /// <summary>
  /// 指定インデックスのRTV CPUハンドルを取得
  /// </summary>
  D3D12_CPU_DESCRIPTOR_HANDLE GetRtvCpuDescriptorHandle(uint32_t index) {
      return GetCPUDescriptorHandle(rtvDescriptorHeap, descriptorSizeRTV, index);
  }

  /// <summary>
  /// デバイスのゲッター
  /// </summary>
  /// <returns></returns>
  /// <returns></returns>
  ID3D12Device *GetDevice() const { return device.Get(); }

  ID3D12GraphicsCommandList *GetCommandList() const {
    return commandList.Get();
  }

  ID3D12CommandQueue *GetCommandQueue() const {
    return commandQueue.Get();
  }

  ID3D12Resource *GetDepthStencilResource() const {
    return depthStencilResource.Get();
  }

  /// <summary>
  /// バックバッファのRTVハンドルを取得
  /// </summary>
  D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferRtvHandle() const {
      UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
      return rtvHandles[backBufferIndex];
  }

  /// <summary>
  /// DSVハンドルの取得
  /// </summary>
  D3D12_CPU_DESCRIPTOR_HANDLE GetDsvCpuDescriptorHandle() {
      return GetCPUDescriptorHandle(dsvDescriptorHeap, descriptorSizeDSV, 0);
  }

  /// <summary>
  /// シェーダーのコンパイル
  /// </summary>
  /// <param name="filePath">compileするshaderファイルへのパス</param>
  /// <param name="profile">compileに使用するprofile</param>
  /// <returns></returns>
  IDxcBlob *CompileShader(const std::wstring &filePath, const wchar_t *profile);

  /// <summary>
  /// リソースの生成
  /// </summary>
  /// <param name="device"></param>
  /// <param name="sizeInBytes"></param>
  /// <returns></returns>
  Microsoft::WRL::ComPtr<ID3D12Resource>
  CreateBufferResource(size_t sizeInBytes);

  /// <summary>
  /// UAV用のリソースを生成 (DEFAULTヒープ)
  /// </summary>
  /// <param name="sizeInBytes"></param>
  /// <returns></returns>
  Microsoft::WRL::ComPtr<ID3D12Resource>
  CreateUAVBufferResource(size_t sizeInBytes);

  /// <summary>
  /// TextureResourceを作る
  /// </summary>
  /// <param name="device"></param>
  /// <param name="metadata"></param>
  /// <returns></returns>
  Microsoft::WRL::ComPtr<ID3D12Resource>
  CreateTextureResource(const DirectX::TexMetadata &metadata);

  /// <summary>
  /// テクスチャデータの転送
  /// </summary>
  /// <param name="texture"></param>
  /// <param name="mipImages"></param>
  [[nodiscard]]
  Microsoft::WRL::ComPtr<ID3D12Resource>
  UploadTextureData(const Microsoft::WRL::ComPtr<ID3D12Resource> &texture,
                    const DirectX::ScratchImage &mipImages);

  /// <summary>
  /// テクスチャファイルの読み込み
  /// </summary>
  /// <param name="filePath">テクスチャファイルパス</param>
  /// <returns>画像イメージデータ</returns>
  static DirectX::ScratchImage LoadTexture(const std::string &filePath);

  /// <summary>
  /// FPSを60固定する
  /// </summary>
  /// <param name="FPS"></param>
  void SetFPS(bool isLockFPS) {
    if (isLockFPS) {
      set60FPS = true;
    } else {
      set60FPS = false;
    }
  };

private:
  // FPS固定初期化
  void InitializeFixFPS();

  // FPS固定更新
  void UpdateFixFPS();

  // 記録時間
  std::chrono::steady_clock::time_point reference_;

  bool set60FPS = false;

  // 3D を描く先（HDR の画面）の RTV
  D3D12_CPU_DESCRIPTOR_HANDLE sceneRtvHandle_{};

  // 全てのリソースの現在の状態を監視する名簿
  std::unordered_map<ID3D12Resource*, D3D12_RESOURCE_STATES> resourceStates_;

public:
  // 最大SRV数(最大テクスチャ枚数)
  // static const uint32_t kMaxSRVCount;

public:
  size_t GetSwapChainResourceNum() const { return swapChainResources.size(); }
};
