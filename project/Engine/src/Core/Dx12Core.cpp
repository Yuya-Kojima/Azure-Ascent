#include "Core/Dx12Core.h"
#include "Debug/Logger.h"
#include "Util/StringUtil.h"
#include "d3dx12.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <Windows.h>
#include <cassert>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <format>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using namespace Microsoft::WRL;

// const uint32_t Dx12Core::kMaxSRVCount = 512;

Microsoft::WRL::ComPtr<ID3D12Resource>
CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> &device,
                                  int32_t width, int32_t height) {

  // 生成するResourceの設定
  D3D12_RESOURCE_DESC resourceDesc{};
  resourceDesc.Width = width;        // Textureの幅
  resourceDesc.Height = height;      // 高さ
  resourceDesc.MipLevels = 1;        // mipmapの数
  resourceDesc.DepthOrArraySize = 1; // 奥行き or 配列Textureの配列数
  resourceDesc.Format =
      DXGI_FORMAT_D24_UNORM_S8_UINT; // DepthStencilとして利用可能なフォーマット
  resourceDesc.SampleDesc.Count = 1; // サンプリングカウント。1固定
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; // 二次元
  resourceDesc.Flags =
      D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う

  // 利用するheapの設定
  D3D12_HEAP_PROPERTIES heapProperties{};
  heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作る

  // 深度値のクリア設定
  D3D12_CLEAR_VALUE depthClearValue{};
  depthClearValue.DepthStencil.Depth = 1.0f; // 最大値(1.0f)でクリア
  depthClearValue.Format =
      DXGI_FORMAT_D24_UNORM_S8_UINT; // フォーマット。Resourceと合わせる

  // Resourceの生成
  Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
  HRESULT hr = device->CreateCommittedResource(
      &heapProperties,                  // heapの設定
      D3D12_HEAP_FLAG_NONE,             // heapの特殊な設定
      &resourceDesc,                    // Resourceの設定
      D3D12_RESOURCE_STATE_DEPTH_WRITE, // 深度値を書き込む状態にしておく
      &depthClearValue,                 // Clear最適値
      IID_PPV_ARGS(&resource));         // 作成するResourceポインタへのポインタ

  assert(SUCCEEDED(hr));

  return resource;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
CreateRenderTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, uint32_t width, uint32_t height, DXGI_FORMAT format, const Vector4& clearColor) {

  // 生成するResourceの設定
  D3D12_RESOURCE_DESC resourceDesc{};
  resourceDesc.Width = width;
  resourceDesc.Height = height;
  resourceDesc.MipLevels = 1;
  resourceDesc.DepthOrArraySize = 1;
  resourceDesc.Format = format;
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET; // RenderTargetとして利用可能にする

  // 利用するHeapの設定
  D3D12_HEAP_PROPERTIES heapProperties{};
  heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT; // 当然VRAM上に作る

  // ClearValueの設定
  D3D12_CLEAR_VALUE clearValue;
  clearValue.Format = format;
  clearValue.Color[0] = clearColor.x;
  clearValue.Color[1] = clearColor.y;
  clearValue.Color[2] = clearColor.z;
  clearValue.Color[3] = clearColor.w;

  // Resourceの作成
  Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
  HRESULT hr = device->CreateCommittedResource(
      &heapProperties,
      D3D12_HEAP_FLAG_NONE,
      &resourceDesc,
      D3D12_RESOURCE_STATE_RENDER_TARGET, // これから描画することを前提としたTextureなのでRenderTargetとして使うことから始める
      &clearValue, // Clear最適値。
      IID_PPV_ARGS(&resource));

  assert(SUCCEEDED(hr));

  return resource;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>
Dx12Core::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType,
                               UINT numDescriptors, bool shaderVisible) {
  // ディスクリプタヒープの生成
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
  D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
  descriptorHeapDesc.Type = heapType;
  descriptorHeapDesc.NumDescriptors = numDescriptors;
  descriptorHeapDesc.Flags = shaderVisible
                                 ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                 : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc,
                                            IID_PPV_ARGS(&descriptorHeap));
  assert(SUCCEEDED(hr));

  return descriptorHeap;
}

D3D12_CPU_DESCRIPTOR_HANDLE Dx12Core::GetCPUDescriptorHandle(
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &descriptorHeap,
    uint32_t descriptorSize, uint32_t index) {

  D3D12_CPU_DESCRIPTOR_HANDLE handleCPU =
      descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  handleCPU.ptr += (descriptorSize * index);
  return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE Dx12Core::GetGPUDescriptorHandle(
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> &descriptorHeap,
    uint32_t descriptorSize, uint32_t index) {

  D3D12_GPU_DESCRIPTOR_HANDLE handleGPU =
      descriptorHeap->GetGPUDescriptorHandleForHeapStart();
  handleGPU.ptr += (descriptorSize * index);
  return handleGPU;
}

void Dx12Core::Initialize(WindowSystem *windowSystem) {

  // FPS固定
  InitializeFixFPS();

  assert(windowSystem);

  this->windowSystem = windowSystem;

  // デバイスの生成
  InitializeDevice();

  // コマンドの生成
  InitializeCommand();

  // スワップチェーンの生成
  InitializeSwapChain();

  // 深度バッファの生成
  InitializeDepthBuffer();

  // 各種ディスクリプタヒープの生成
  InitializeDescriptorHeap();

  // RTVの初期化
  InitializeRenderTargetView();

  // DSVの初期化
  InitializeDepthStencilView();

  // フェンスの初期化
  InitializeFence();

  // ビューポート矩形の初期化
  InitializeViewport();

  // シザー矩形の描画
  InitializeScissorRect();

  // DXCコンパイラの初期化
  InitializeDXCcompiler();

  // ImGuiの初期化
  InitializeImGui();
}

void Dx12Core::BeginFrame() {

  // バックバッファの番号取得
  UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

  // リソース状態を自動で追跡し、RENDER_TARGETへ安全に遷移させる
  TransitionResource(swapChainResources[backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

  // 描画先のRTVとDSVを指定する
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDsvCpuDescriptorHandle();
  
  // 画面全体の深度をクリア
  commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  // SRV用のディスクリプタヒープを指定する
  // 描画用のDescriptorHeapの設定
  // ID3D12DescriptorHeap *descriptorHeaps[] = {srvDescriptorHeap.Get()};
  // commandList->SetDescriptorHeaps(1, descriptorHeaps);

  // ビューポート領域の設定
  commandList->RSSetViewports(1, &viewport);

  // シザー矩形の設定
  commandList->RSSetScissorRects(1, &scissorRect);
}

void Dx12Core::PreDrawImGui() {
  UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

  // 描画先をSwapchainに変更（深度バッファは使わないのでnullptr）
  commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);

  // Swapchainの色をクリア (今まで通りの青っぽい色)
  float clearColor[] = {0.1f, 0.25f, 0.5f, 1.0f}; 
  commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

  // ViewportとScissorRectの再設定
  commandList->RSSetViewports(1, &viewport);
  commandList->RSSetScissorRects(1, &scissorRect);
}

void Dx12Core::EndFrame() {

  HRESULT hr;

  // バックバッファの番号取得
  UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

  // 画面表示用に PRESENT 状態へ安全に遷移
  TransitionResource(swapChainResources[backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT);

  // コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
  hr = commandList->Close();
  assert(SUCCEEDED(hr));

  // GPUにコマンドリストの実行を行わせる
  ID3D12CommandList *commandLists[] = {commandList.Get()};
  commandQueue->ExecuteCommandLists(1, commandLists);

  // GPUとOSに画面の交換を行うように通知する
  swapChain->Present(1, 0);

  // Fenceの値を更新
  fenceValue++;

  // GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
  commandQueue->Signal(fence.Get(), fenceValue);

  // Fenceの値が指定したSignal値にたどり着いたか確認する
  // GetCompletedValueの初期値はFenceの作成時に渡した初期値
  if (fence->GetCompletedValue() < fenceValue) {
    // 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
    fence->SetEventOnCompletion(fenceValue, fenceEvent);

    // イベントを待つ
    WaitForSingleObject(fenceEvent, INFINITE);
  }

  // FPS固定
  if (set60FPS) {
    UpdateFixFPS();
  }

  // 次のフレーム用のコマンドリストを準備
  // コマンドアロケータのリセット
  hr = commandAllocator->Reset();
  assert(SUCCEEDED(hr));

  // コマンドリストのリセット
  hr = commandList->Reset(commandAllocator.Get(), nullptr);
  assert(SUCCEEDED(hr));
}

void Dx12Core::InitializeDevice() {
  HRESULT hr;

#ifdef _DEBUG

  Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {

    // デバッグレイヤーを有効化する
    debugController->EnableDebugLayer();

    // さらにGPU側でもチェックを行うようにする
    debugController->SetEnableGPUBasedValidation(TRUE);
  }

#endif

  // DXGIファクトリーの生成
  dxgiFactory = nullptr;

  hr = CreateDXGIFactory(IID_PPV_ARGS(dxgiFactory.GetAddressOf()));

  assert(SUCCEEDED(hr));

  // 使用するアダプタ用の変数。最初にnullptrを入れておく
  ComPtr<IDXGIAdapter4> useAdapter = nullptr;

  // 良い順にアダプタを頼む
  for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(
                       i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                       IID_PPV_ARGS(&useAdapter)) != DXGI_ERROR_NOT_FOUND;
       ++i) {

    // アダプターの情報を取得する
    DXGI_ADAPTER_DESC3 adapterDesc{};
    hr = useAdapter->GetDesc3(&adapterDesc);
    assert(SUCCEEDED(hr));

    // ソフトウェアアダプタでなければ採用
    if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
      // 採用したアダプタの情報をログに出力。wstring
      Logger::Log(StringUtil::ConvertString(
          std::format(L"Use Adapter:{}\n,", adapterDesc.Description)));
      break;
    }

    useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
  }

  // 適切なアダプタが見つからなかったので起動できない
  assert(useAdapter != nullptr);

  device = nullptr;

  // 機能レベルとログ出力用の文字列
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0};

  const char *featureLevelStrings[] = {"12.2", "12.1", "12.0"};

  // 高い順に生成できるか試していく
  for (size_t i = 0; i < _countof(featureLevels); ++i) {

    // 採用したアダプターデバイスを生成
    hr = D3D12CreateDevice(useAdapter.Get(), featureLevels[i],
                           IID_PPV_ARGS(device.GetAddressOf()));

    // 指定した機能レベルでデバイスが生成できたかを確認
    if (SUCCEEDED(hr)) {
      // 生成できたのでログ出力を行ってループを抜ける
      Logger::Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
      break;
    }
  }

  // デバイスの生成がうまくいかなかったので起動できない
  assert(device != nullptr);
  Logger::Log("Complete create D3D12Device!!!\n"); // 初期化完了のログ

#ifdef _DEBUG

  Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;

  if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {

    // ヤバいエラー時に止まる
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

    // エラー時に止まる
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

    // 警告時に止まる
    infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

    // 抑制するメッセージのID
    D3D12_MESSAGE_ID denyIds[] = {

        // Windows11でのDXGIデバッグレイヤーとDX12のデバッグレイヤーの相互作用バグによるエラーメッセージ
        D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE

    };

    // 抑制するレベル
    D3D12_MESSAGE_SEVERITY severities[] = {D3D12_MESSAGE_SEVERITY_INFO};
    D3D12_INFO_QUEUE_FILTER filter{};
    filter.DenyList.NumIDs = _countof(denyIds);
    filter.DenyList.pIDList = denyIds;
    filter.DenyList.NumSeverities = _countof(severities);
    filter.DenyList.pSeverityList = severities;

    // 指定したメッセージの表示を抑制する
    infoQueue->PushStorageFilter(&filter);

    // 解放
    //  infoQueue->Release();
  }

#endif // DEBUG
}

void Dx12Core::InitializeCommand() {

  HRESULT hr;

  // コマンドキューを生成する
  commandQueue = nullptr;
  D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
  hr = device->CreateCommandQueue(&commandQueueDesc,
                                  IID_PPV_ARGS(commandQueue.GetAddressOf()));

  // コマンドキューの生成がうまくいかなかったので起動できない
  assert(SUCCEEDED(hr));

  // コマンドアロケータを生成する
  commandAllocator = nullptr;
  hr = device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT,
      IID_PPV_ARGS(commandAllocator.GetAddressOf()));

  // コマンドアロケータの生成がうまくいかなかったので起動できない
  assert(SUCCEEDED(hr));

  // コマンドリストを生成する
  commandList = nullptr;
  hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 commandAllocator.Get(), nullptr,
                                 IID_PPV_ARGS(commandList.GetAddressOf()));

  // コマンドリストの生成がうまくいかなかったので起動できない
  assert(SUCCEEDED(hr));
}

void Dx12Core::InitializeSwapChain() {

  HRESULT hr;

  // スワップチェーンを生成する
  swapChainDesc.Width = WindowSystem::
      kClientWidth; // 画面の幅。ウィンドウのクライアント領域を同じものにしておく
  swapChainDesc.Height = WindowSystem::
      kClientHeight; // 画面の高さ。ウィンドウのクライアント領域を同じものにしておく
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 色の形式
  swapChainDesc.SampleDesc.Count = 1;                // マルチサンプルしない
  swapChainDesc.BufferUsage =
      DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
  swapChainDesc.BufferCount = 2;       // ダブルバッファ
  swapChainDesc.SwapEffect =
      DXGI_SWAP_EFFECT_FLIP_DISCARD; // モニタにうつしたら、中身を破壊

  // コマンドキュー、ウィンドウハンドル、設定を渡して生成する
  hr = dxgiFactory->CreateSwapChainForHwnd(
      commandQueue.Get(), windowSystem->GetHwnd(), &swapChainDesc, nullptr,
      nullptr, reinterpret_cast<IDXGISwapChain1 **>(swapChain.GetAddressOf()));
  assert(SUCCEEDED(hr));
}

void Dx12Core::InitializeDepthBuffer() {
  // DepthStencilTextureをウィンドウサイズで作成
  depthStencilResource = CreateDepthStencilTextureResource(
      device, WindowSystem::kClientWidth, WindowSystem::kClientHeight);

  // 初期状態を記録
  resourceStates_[depthStencilResource.Get()] = D3D12_RESOURCE_STATE_DEPTH_WRITE;
}

void Dx12Core::InitializeDescriptorHeap() {

  // DescriptorSizeの取得
  descriptorSizeRTV =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  // descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(
  //    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  descriptorSizeDSV =
      device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // RTV用のヒープでディスクリプタの数は64（Swapchainx2 + RenderTexture複数対応）。RTVはShader内で触るものではないので、ShaderVisibleはfalse
  rtvDescriptorHeap =
      CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false);

  // SRV用のヒープでディスクリプタの数は128。STVはShader内で触るものなので、ShaderVisibleはtrue
  // srvDescriptorHeap =
  //   CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
  //   kMaxSRVCount, true);

  // DSV用のヒープのでディスクリプタの数は1。DSVはShader内で触るものではないので、shaderVisibleはfalse
  dsvDescriptorHeap =
      CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
}

void Dx12Core::InitializeRenderTargetView() {

  HRESULT hr;

  for (uint32_t i = 0; i < 2; ++i) {
    // SwapChainからResourceを引っ張ってくる
    hr = swapChain->GetBuffer(
        i, IID_PPV_ARGS(swapChainResources[i].GetAddressOf()));

    // うまく取得できなければ起動できない
    assert(SUCCEEDED(hr));
  }

  // RTVの設定
  rtvDesc.Format =
      DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; // 出力結果をSRGBに変換して書き込む
  rtvDesc.ViewDimension =
      D3D12_RTV_DIMENSION_TEXTURE2D; // 2dテクスチャとして書き込む

  // SwapChain用のRTV割り当てと生成
  for (uint32_t i = 0; i < 2; ++i) {
    rtvHandles[i] = GetRtvCpuDescriptorHandle(AllocateRTV());
    device->CreateRenderTargetView(swapChainResources[i].Get(), &rtvDesc,
                                   rtvHandles[i]);
                                   
    // スワップチェーンバッファの初期状態はCOMMON
    resourceStates_[swapChainResources[i].Get()] = D3D12_RESOURCE_STATE_COMMON;
  }
}

// D3D12_CPU_DESCRIPTOR_HANDLE
// Dx12Core::GetSRVCPUDescriptorHandle(uint32_t index) {
//   return GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
// }

// D3D12_GPU_DESCRIPTOR_HANDLE
// Dx12Core::GetSRVGPUDescriptorHandle(uint32_t index) {
//   return GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, index);
// }

void Dx12Core::InitializeDepthStencilView() {

  // DSVの設定
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
  dsvDesc.Format =
      DXGI_FORMAT_D24_UNORM_S8_UINT; // format。基本的にはResourceに合わせる
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; // 2dTexture

  // DSVHeapの先頭にDSVを作る
  device->CreateDepthStencilView(
      depthStencilResource.Get(), &dsvDesc,
      dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void Dx12Core::InitializeFence() {
  HRESULT hr;

  // 初期値0でFenceを作る
  fenceValue = 0;
  hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE,
                           IID_PPV_ARGS(&fence));
  assert(SUCCEEDED(hr));

  // FenceのSignalを待つためのイベントを作成
  fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  assert(fenceEvent != nullptr);
}

void Dx12Core::InitializeViewport() {

  // クライアント領域のサイズと一緒にして画面全体に表示
  viewport.Width = WindowSystem::kClientWidth;
  viewport.Height = WindowSystem::kClientHeight;
  viewport.TopLeftX = 0;
  viewport.TopLeftY = 0;
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
}

void Dx12Core::InitializeScissorRect() {

  // 基本的にビューポートと同じ矩形が構成されるようにする
  scissorRect.left = 0;
  scissorRect.right = WindowSystem::kClientWidth;
  scissorRect.top = 0;
  scissorRect.bottom = WindowSystem::kClientHeight;
}

void Dx12Core::InitializeDXCcompiler() {
  HRESULT hr;

  // dxCompilerを初期化
  hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
  assert(SUCCEEDED(hr));

  hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
  assert(SUCCEEDED(hr));

  // 現時点でincludeはしないが、includeに対応するための設定をしておく
  hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
  assert(SUCCEEDED(hr));
}

void Dx12Core::InitializeImGui() {

  // ImGuiの初期化

  // バージョンチェック
  // IMGUI_CHECKVERSION();

  //// コンテキストの生成
  // ImGui::CreateContext();

  //// スタイルの設定
  // ImGui::StyleColorsDark();

  //// Win32用の初期化
  // ImGui_ImplWin32_Init(windowSystem->GetHwnd());

  // DirectX12用の初期化
  // ImGui_ImplDX12_Init(device.Get(), swapChainDesc.BufferCount,
  // rtvDesc.Format,
  //                    srvDescriptorHeap.Get(),
  //                    srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
  //                    srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart()

  //);
}

IDxcBlob *Dx12Core::CompileShader(const std::wstring &filePath,
                                  const wchar_t *profile) {

  //======================================
  // 1.hlslファイルを読む
  //======================================

  // これからシェーダーをコンパイルする旨をログに出す
  Logger::Log(StringUtil::ConvertString(std::format(
      L"Begin CompileShader, path:{}, profile:{} \n", filePath, profile)));

  // hlslファイルを読む
  Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
  HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

  // 読めなかったら止める
  assert(SUCCEEDED(hr));

  // 読み込んだファイルの内容を設定する
  DxcBuffer shaderSourceBuffer;
  shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
  shaderSourceBuffer.Size = shaderSource->GetBufferSize();
  shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

  //======================================
  // 2.Compileする
  //======================================

  LPCWSTR arguments[] = {

      filePath.c_str(), // コンパイル対象のhlslファイル名
      L"-E",
      L"main", // エントリーポイントの指定。基本的にmain以外にはしない
      L"-T",
      profile, // shderProfileの設定
      L"-Zi",
      L"-Qembed_debug", // デバッグ用の情報を埋め込む
#ifdef _DEBUG
      L"-Od",           // 最適化を外しておく
#else
      L"-O3",           // 最適化する
#endif
      L"-Zpr",          // メモリレイアウトは最優先
  };

  // 実際にshaderをコンパイルする
  Microsoft::WRL::ComPtr<IDxcResult> shaderResult = nullptr;
  hr = dxcCompiler->Compile(&shaderSourceBuffer,  // 読み込んだファイル
                            arguments,            // コンパイルオプション
                            _countof(arguments),  // コンパイルオプションの数
                            includeHandler.Get(), // includeが含まれた諸々
                            IID_PPV_ARGS(&shaderResult));

  // コンパイルエラーではなくdxcが起動できないなど致命的な状況
  assert(SUCCEEDED(hr));

  //======================================
  // 3.警告、エラーが出ていないか確認する
  //======================================

  // 警告、エラーが出てたらログにだして止める
  Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr;
  shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);

  if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
    Logger::Log(shaderError->GetStringPointer());
    const char *msg = shaderError->GetStringPointer();
    Logger::Log(msg); // ← ここでエラーメッセージを出力
    OutputDebugStringA(msg);
    assert(false);
  }

  //======================================
  // 4.Compile結果を受け取って渡す
  //======================================

  // コンパイル結果から実行用のバイナリ部分を取得
  Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr;
  hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob),
                               nullptr);

  assert(SUCCEEDED(hr));

  // 成功したログを出す
  Logger::Log(StringUtil::ConvertString(std::format(
      L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

  // 実行用のバイナリを返却
  return shaderBlob.Detach();
}

Microsoft::WRL::ComPtr<ID3D12Resource>
Dx12Core::CreateBufferResource(size_t sizeInBytes) {

  D3D12_HEAP_PROPERTIES uploadHeapProperties{};
  uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bufferResourceDesc{};

  bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferResourceDesc.Width = sizeInBytes;

  bufferResourceDesc.Height = 1;
  bufferResourceDesc.DepthOrArraySize = 1;
  bufferResourceDesc.MipLevels = 1;
  bufferResourceDesc.SampleDesc.Count = 1;

  bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource = nullptr;

  HRESULT hr = device->CreateCommittedResource(
      &uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferResourceDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&bufferResource));
  if (FAILED(hr)) {
      char msg[256];
      sprintf_s(msg, "CreateBufferResource failed! HRESULT: 0x%08X", hr);
      MessageBoxA(nullptr, msg, "DirectX 12 Error", MB_OK | MB_ICONERROR);
      abort();
  }

  // 初期状態を記録
  resourceStates_[bufferResource.Get()] = D3D12_RESOURCE_STATE_GENERIC_READ;

  return bufferResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
Dx12Core::CreateUAVBufferResource(size_t sizeInBytes) {

  D3D12_HEAP_PROPERTIES defaultHeapProperties{};
  defaultHeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC bufferResourceDesc{};

  bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferResourceDesc.Width = sizeInBytes;

  bufferResourceDesc.Height = 1;
  bufferResourceDesc.DepthOrArraySize = 1;
  bufferResourceDesc.MipLevels = 1;
  bufferResourceDesc.SampleDesc.Count = 1;

  bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  // UAVとして使用するためにフラグを設定
  bufferResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource = nullptr;

  HRESULT hr = device->CreateCommittedResource(
      &defaultHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferResourceDesc,
      D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&bufferResource));
  if (FAILED(hr)) {
      char msg[256];
      sprintf_s(msg, "CreateUAVBufferResource failed! HRESULT: 0x%08X", hr);
      MessageBoxA(nullptr, msg, "DirectX 12 Error", MB_OK | MB_ICONERROR);
      abort();
  }

  // 初期状態を記録
  resourceStates_[bufferResource.Get()] = D3D12_RESOURCE_STATE_COMMON;

  return bufferResource;
}

Microsoft::WRL::ComPtr<ID3D12Resource>
Dx12Core::CreateTextureResource(const DirectX::TexMetadata &metadata) {

  // 1.metadataを基にResourceの設定
  D3D12_RESOURCE_DESC resourceDesc{};
  resourceDesc.Width = UINT(metadata.width);           // Textureの幅
  resourceDesc.Height = UINT(metadata.height);         // Textureの高さ
  resourceDesc.MipLevels = UINT16(metadata.mipLevels); // mipmapの数
  resourceDesc.DepthOrArraySize =
      UINT16(metadata.arraySize);        // 奥行 or 配列Textureの配列数
  resourceDesc.Format = metadata.format; // TextureのFormat
  resourceDesc.SampleDesc.Count = 1;     // サンプリングカウント。1固定
  resourceDesc.Dimension =
      D3D12_RESOURCE_DIMENSION(metadata.dimension); // textureの次元数

  // 利用するHeapの設定。
  D3D12_HEAP_PROPERTIES heapProperties{};
  heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

  // Resourceの生成
  Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
  HRESULT hr = device->CreateCommittedResource(
      &heapProperties,                // Heapの設定
      D3D12_HEAP_FLAG_NONE,           // Heapの特殊な設定
      &resourceDesc,                  // Resourceの設定
      D3D12_RESOURCE_STATE_COPY_DEST, // データ転送される設定
      nullptr,                        // Clear最適値。使用しないのでnullptr
      IID_PPV_ARGS(&resource));       // 作成するResourceポインタへのポインタ

  assert(SUCCEEDED(hr));
  
  // 初期状態を記録
  resourceStates_[resource.Get()] = D3D12_RESOURCE_STATE_COPY_DEST;
  
  return resource;
}

[[nodiscard]]
Microsoft::WRL::ComPtr<ID3D12Resource> Dx12Core::UploadTextureData(
    const Microsoft::WRL::ComPtr<ID3D12Resource> &texture,
    const DirectX::ScratchImage &mipImages) {

  std::vector<D3D12_SUBRESOURCE_DATA> subresources;

  const DirectX::Image *images = mipImages.GetImages();
  const size_t imageCount = mipImages.GetImageCount();

  subresources.reserve(imageCount);

  for (size_t i = 0; i < imageCount; ++i) {
    D3D12_SUBRESOURCE_DATA subresource{};
    subresource.pData = images[i].pixels;
    subresource.RowPitch = static_cast<LONG_PTR>(images[i].rowPitch);
    subresource.SlicePitch = static_cast<LONG_PTR>(images[i].slicePitch);
    subresources.push_back(subresource);
  }

  uint64_t intermediateSize =
      GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));

  Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource =
      CreateBufferResource(intermediateSize);

  UpdateSubresources(commandList.Get(), texture.Get(),
                     intermediateResource.Get(), 0, 0,
                     UINT(subresources.size()), subresources.data());

  // Textureへの転送後は利用できるよう、COPY_DEST から GENERIC_READ へ遷移
  TransitionResource(texture.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);

  return intermediateResource;
}

void Dx12Core::InitializeFixFPS() {

  // 現在時間を記録する
  reference_ = std::chrono::steady_clock::now();
}

void Dx12Core::UpdateFixFPS() {

  // 1/60秒
  const std::chrono::microseconds kMinTime(uint64_t(1000000.0f / 60.0f));

  // 1/60秒よりわずかに短い時間
  const std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 65.0f));

  // 現在時間を取得する
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

  // 前回記録からの経過時間を取得する
  std::chrono::microseconds elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

  // 1/60秒（よりわずかに短い時間）経っていない場合
  if (elapsed < kMinCheckTime) {
    // 1/60秒経過するまで微小なスリープを繰り返す
    while (std::chrono::steady_clock::now() - reference_ < kMinTime) {
      // 1マイクロ秒スリープ
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }

  // 現在の時間を記録する
  reference_ = std::chrono::steady_clock::now();
}

DirectX::ScratchImage Dx12Core::LoadTexture(const std::string &filePath) {

  // テクスチャファイルを読んでプログラムで扱えるようにする
  DirectX::ScratchImage image{};
  std::wstring filePathW = StringUtil::ConvertString(filePath);
  // HRESULT hr = DirectX::LoadFromWICFile(
  //     filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
  // assert(SUCCEEDED(hr));

  HRESULT hr;
  if (filePathW.ends_with(
          L".dds")) { // .ddsで終わっていたらddsとみなす。より安全な方法はいくらでもあるので余裕があれば対応すると良い
    hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE,
                                  nullptr, image);
  } else {
    hr = DirectX::LoadFromWICFile(
        filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
  }

  assert(SUCCEEDED(hr));

  // ミニマップの作成
  DirectX::ScratchImage mipImages{};

  if (DirectX::IsCompressed(
          image.GetMetadata().format)) { // 圧縮フォーマットかどうかを調べる
    mipImages =
        std::move(image); // 圧縮フォーマットならそのまま使うのでmoveする
  } else {
    hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(),
                                  image.GetMetadata(), DirectX::TEX_FILTER_SRGB,
                                  0, mipImages);
  }

  assert(SUCCEEDED(hr));

  // ミニマップ付きのデータを返す
  return mipImages;
}

void Dx12Core::BeginSceneDepthRead() {
  // 深度を描画先から外してから、シェーダーで読む状態にする
  commandList->OMSetRenderTargets(1, &sceneRtvHandle_, false, nullptr);
  TransitionResource(depthStencilResource.Get(),
                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void Dx12Core::EndSceneDepthRead() {
  TransitionResource(depthStencilResource.Get(),
                     D3D12_RESOURCE_STATE_DEPTH_WRITE);
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDsvCpuDescriptorHandle();
  commandList->OMSetRenderTargets(1, &sceneRtvHandle_, false, &dsvHandle);
}

void Dx12Core::TransitionResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES newState) {
  if (!resource) return;

  // 1. 名簿から現在の状態を取得
  D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON; // 初期値の仮定
  auto it = resourceStates_.find(resource);
  if (it != resourceStates_.end()) {
      currentState = it->second;
  } else {
      // 登録されていない場合はCOMMONとみなして登録する（安全策）
      resourceStates_[resource] = currentState;
  }

  // 2. 現在の状態と新しい状態が同じなら何もしない（超軽量）
  if (currentState == newState) {
      return;
  }

  // 3. 状態が異なる場合はバリアを張る
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = currentState;
  barrier.Transition.StateAfter = newState;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  commandList->ResourceBarrier(1, &barrier);

  // 4. 名簿の状態を更新
  resourceStates_[resource] = newState;
}