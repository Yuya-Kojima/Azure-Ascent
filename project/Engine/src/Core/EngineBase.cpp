#include "Core/EngineBase.h"
#include "Audio/SoundManager.h"
#include "Camera/GameCamera.h"
#include "Core/WindowSystem.h"
#include "Model/ModelManager.h"
#include "Texture/TextureManager.h"
#include "Render/Text/FontManager.h"
#include "Framework/UIManager.h"
#include <cassert>
#include <xaudio2.h>

EngineBase::~EngineBase() = default;

void EngineBase::Initialize() {

  // hrの生成
  HRESULT hr;

#ifdef _DEBUG
  leakChecker_ = std::make_unique<D3DResourceLeakChecker>();
#endif

  //===========================
  // WindowsAPIの初期化
  //===========================
  windowSystem_ = std::make_unique<WindowSystem>();
  windowSystem_->Initialize();

  //===========================
  // DirectXの初期化
  //===========================
  dx12Core_ = std::make_unique<Dx12Core>();
  dx12Core_->Initialize(windowSystem_.get());

  ID3D12Device *device = dx12Core_->GetDevice();
  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  //===========================
  // キーボード入力の初期化
  //===========================
  input_ = std::make_unique<Input>();
  input_->Initialize(windowSystem_.get());

  //===========================
  // Audioの初期化
  //===========================
  IXAudio2MasteringVoice
      *masterVoice; // xAudio2が解放されると同時に無効化されるのでdeleteしない。

  hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
  assert(SUCCEEDED(hr));
  hr = xAudio2_->CreateMasteringVoice(&masterVoice);
  assert(SUCCEEDED(hr));

  SoundManager::GetInstance()->Initialize(xAudio2_.Get());

  //===========================
  // SRVManagerの初期化
  //===========================
  srvManager_ = std::make_unique<SrvManager>();
  srvManager_->Initialize(dx12Core_.get());

  spriteRenderer_ = std::make_unique<SpriteRenderer>();
  spriteRenderer_->Initialize(dx12Core_.get());

  object3dRenderer_ = std::make_unique<Object3dRenderer>();
  object3dRenderer_->Initialize(dx12Core_.get());

  skyboxRenderer_ = std::make_unique<SkyboxRenderer>();
  skyboxRenderer_->Initialize(dx12Core_.get());

  cloudSeaRenderer_ = std::make_unique<CloudSeaRenderer>();
  cloudSeaRenderer_->Initialize(dx12Core_.get(), object3dRenderer_.get(),
                                srvManager_.get());

  // テクスチャマネージャーの初期化
  TextureManager::GetInstance()->Initialize(dx12Core_.get(), srvManager_.get());

  // モデルマネージャーの初期化
  ModelManager::GetInstance()->Initialize(dx12Core_.get());

  // パーティクルマネージャーの初期化
  ParticleManager::GetInstance()->Initialize(dx12Core_.get(),
                                             srvManager_.get());

  // ラインレンダラーの初期化
  LineRenderer::GetInstance()->Initialize(dx12Core_.get());

  // トレイルレンダラーの初期化
  TrailRenderer::GetInstance()->Initialize(dx12Core_.get());

  // フォントマネージャーの初期化とフォントのロード
  FontManager::GetInstance()->Initialize();

  // UIManagerの初期化
  UIManager::GetInstance()->Initialize(spriteRenderer_.get());
}

void EngineBase::Finalize() {
  UIManager::GetInstance()->Finalize();
  FontManager::GetInstance()->Finalize();

  SoundManager::GetInstance()->Finalize();

  xAudio2_.Reset();

  ParticleManager::GetInstance()->Finalize();

  ModelManager::GetInstance()->Finalize();

  TextureManager::GetInstance()->Finalize();

  LineRenderer::GetInstance()->Finalize();
  TrailRenderer::GetInstance()->Finalize();

  // delete input_;
  // input_ = nullptr;

  // delete srvManager_;
  // srvManager_ = nullptr;

  // delete object3dRenderer_;
  // object3dRenderer_ = nullptr;

  // delete spriteRenderer_;
  // spriteRenderer_ = nullptr;

  // delete dx12Core_;
  // dx12Core_ = nullptr;

  // windowSystem_->Finalize();

  // delete windowSystem_;
  // windowSystem_ = nullptr;

  input_.reset();
  object3dRenderer_.reset();
  spriteRenderer_.reset();
  skyboxRenderer_.reset();
  cloudSeaRenderer_.reset();
  srvManager_.reset();

  dx12Core_.reset();

  if (windowSystem_) {
    windowSystem_->Finalize();
    windowSystem_.reset();
  }

#ifdef _DEBUG
  // delete leakChecker_;
  // leakChecker_ = nullptr;
  leakChecker_.reset();
#endif
}

void EngineBase::Update() {

  // Windowにメッセージが来てたら最優先で処理させる
  if (windowSystem_ && windowSystem_->ProcessMessage()) {
    endRequest_ = true;
    return;
  }

  // キー入力
  input_->Update();
}

void EngineBase::BeginFrame() {
  // DirectXの描画準備。すべてに共通のグラフィックスコマンドを積む
  dx12Core_->BeginFrame();

  // SRVをコマンドリストにセット
  srvManager_->PreDraw();
}

void EngineBase::EndFrame() { dx12Core_->EndFrame(); }

void EngineBase::Begin3D() {
  // 3DObjectの描画準備。3DObjectの描画に共通のグラフィックスコマンドを積む
  object3dRenderer_->Begin();
}

void EngineBase::Begin2D() {
  // Spriteの描画準備。Spriteの描画に共通のグラフィックスコマンドを積む
  spriteRenderer_->Begin();
}

void EngineBase::End3D() {
  // 今は何もしない
}

void EngineBase::End2D() {
  // 今は何もしない
}

void EngineBase::Run() {
  Initialize();

  while (true) {
    Update();
    if (IsEndRequest()) {
      break;
    }
    Draw();
  }

  Finalize();
}
