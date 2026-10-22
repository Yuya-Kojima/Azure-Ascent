#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Render/Texture/RenderTexture.h"
#include "Render/Renderer/Bloom.h"
#include "Renderer/PostProcess.h"
#include <memory>

class RenderPipeline {
public:
  RenderPipeline() = default;
  ~RenderPipeline() = default;

  // 初期化（レンダーターゲットやブルームの生成）
  void Initialize(Dx12Core* dx12Core, SrvManager* srvManager);

  // 3D描画パスの開始（HDRキャンバスをターゲットにセット）
  void Begin3DPass(Dx12Core* dx12Core);

  // ポストプロセス処理（Bloom, モーションブラー等）を実行し、メイン画面へ焼き付ける
  void DrawPostProcess(Dx12Core* dx12Core, SrvManager* srvManager, PostProcess* currentScenePostProcess);

  // エディタモードの有効/無効を設定
  void SetEditorMode(bool isEditorMode) { isEditorMode_ = isEditorMode; }
  
  // エディタ画面表示用テクスチャのSRVインデックスを取得
  uint32_t GetEditorGameViewSrvIndex() const;

  // オーバーレイ描画後、エディタテクスチャをSRVに遷移させ、ターゲットをバックバッファに戻す
  void EndEditorGameViewPass(Dx12Core* dx12Core);

private:
  bool isEditorMode_ = false;
  std::unique_ptr<RenderTexture> editorGameViewTexture_ = nullptr;
  std::unique_ptr<RenderTexture> mainRenderTexture_ = nullptr;
  std::unique_ptr<Bloom> bloom_ = nullptr;
  std::unique_ptr<RenderTexture> historyTextures_[2] = { nullptr, nullptr };
  
  uint32_t currentHistoryIndex_ = 0;
  SrvHandle depthTextureSrv_;
};
