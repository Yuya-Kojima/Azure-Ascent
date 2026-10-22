#pragma once
#ifdef USE_IMGUI
#include "imgui.h"
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#endif // USE_IMGUI

#include "Core/Dx12Core.h"
#include "Core/SrvManager.h"
#include "Core/WindowSystem.h"

class ImGuiManager {
public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(WindowSystem *winApp, Dx12Core *dx12Core,
                  SrvManager *srvManager);

  /// <summary>
  /// 終了
  /// </summary>
  void Finalize();

  /// <summary>
  /// ImGui受付開始
  /// </summary>
  void Begin();

  /// <summary>
  /// ImGui受付終了
  /// </summary>
  void End();

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

private:
  Dx12Core *dx12Core_ = nullptr;
  ID3D12DescriptorHeap *srvHeap_ = nullptr;
  // フォントのテクスチャの SRV の番号
  SrvHandle fontSrv_;
};
