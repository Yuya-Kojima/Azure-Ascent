#include "Debug/ImGuiManager.h"
#include <imgui_internal.h>

void ImGuiManager::Initialize([[maybe_unused]] WindowSystem *winApp,
                              [[maybe_unused]] Dx12Core *dx12Core,
                              [[maybe_unused]] SrvManager *srvManager) {

#ifdef USE_IMGUI

  // ポインタ
  dx12Core_ = dx12Core;
  srvHeap_ = srvManager->GetDescriptorHeap();

  // ImGuiのコンテキストを生成
  ImGui::CreateContext();

  // Docking機能を有効化
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows

  // 日本語フォント（メイリオ）の読み込み
  io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());

  // ImGuiのスタイルを設定
  ImGui::StyleColorsDark();
  
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.TabRounding = 4.0f;

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text]                   = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_WindowBg]               = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
  colors[ImGuiCol_ChildBg]                = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
  colors[ImGuiCol_FrameBg]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  colors[ImGuiCol_FrameBgActive]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
  colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_TitleBgActive]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
  colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
  colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  colors[ImGuiCol_CheckMark]              = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_SliderGrab]             = ImVec4(0.24f, 0.52f, 0.88f, 1.00f);
  colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
  colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_ButtonHovered]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  colors[ImGuiCol_ButtonActive]           = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
  colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_HeaderHovered]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  colors[ImGuiCol_HeaderActive]           = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
  colors[ImGuiCol_Tab]                    = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
  colors[ImGuiCol_TabHovered]             = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  colors[ImGuiCol_TabUnfocused]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
  colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  colors[ImGuiCol_DockingPreview]         = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
  colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);

  ImGui_ImplWin32_Init(winApp->GetHwnd());

  fontSrv_ = srvManager->Allocate();
  uint32_t srvIndex = fontSrv_.Index();

  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
      srvManager->GetCPUDescriptorHandle(srvIndex);

  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
      srvManager->GetGPUDescriptorHandle(srvIndex);

  ImGui_ImplDX12_InitInfo init_info = {};
  init_info.Device = dx12Core->GetDevice();
  init_info.CommandQueue = dx12Core->GetCommandQueue();
  init_info.NumFramesInFlight = static_cast<int>(dx12Core->GetSwapChainResourceNum());
  init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
  init_info.SrvDescriptorHeap = srvHeap_;
  init_info.LegacySingleSrvCpuDescriptor = cpuHandle;
  init_info.LegacySingleSrvGpuDescriptor = gpuHandle;
  ImGui_ImplDX12_Init(&init_info);

#endif // USE_IMGUI
}

void ImGuiManager::Finalize() {
#ifdef USE_IMGUI

  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  fontSrv_.Reset();

#endif // USE_IMGUI
}

void ImGuiManager::Begin() {
#ifdef USE_IMGUI

  // ImGuiフレーム開始
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // 画面全体をDockingの領域として設定
  ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");
  ImGui::DockSpaceOverViewport(dockspace_id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

  // 初回起動時（imgui.iniが存在しないか、明示的なレイアウトリセット時）にデフォルトのドッキングレイアウトを構築する
  static bool first_time = true;
  if (first_time) {
      first_time = false;

      // すでにレイアウトが構築されているか（imgui.iniが読み込まれているか）を判定する
      // Central Node がまだ作成されていない、もしくは設定がロードされていない場合のみ構築を行う
      if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr || ImGui::DockBuilderGetNode(dockspace_id)->IsSplitNode() == false) {
          ImGui::DockBuilderRemoveNode(dockspace_id);
          ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
          ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

          ImGuiID dock_main_id = dockspace_id;
          
          ImGuiID dock_id_top = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, 0.05f, nullptr, &dock_main_id);
          ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);
          ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.2f, nullptr, &dock_main_id);
          ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);

          ImGuiID dock_id_right_bottom = ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Down, 0.5f, nullptr, &dock_id_right);

          ImGui::DockBuilderDockWindow("Main Toolbar", dock_id_top);
          ImGui::DockBuilderDockWindow("Game View", dock_main_id);
          ImGui::DockBuilderDockWindow("Hierarchy", dock_id_left);
          ImGui::DockBuilderDockWindow("Inspector", dock_id_right);
          ImGui::DockBuilderDockWindow("World Settings", dock_id_right_bottom);
          ImGui::DockBuilderDockWindow("Project", dock_id_bottom);
          ImGui::DockBuilderDockWindow("Timeline & Sequencer", dock_id_bottom);
          ImGui::DockBuilderDockWindow("Console", dock_id_bottom);
          
          ImGui::DockBuilderFinish(dockspace_id);
      }
  }

#endif // USE_IMGUI
}

void ImGuiManager::End() {
#ifdef USE_IMGUI

  // 描画前準備
  ImGui::Render();

#endif // USE_IMGUI
}

void ImGuiManager::Draw() {
#ifdef USE_IMGUI

  ID3D12GraphicsCommandList *commandList = dx12Core_->GetCommandList();

  // デスクリプタヒープの配列をセットするコマンド
  ID3D12DescriptorHeap *ppHeaps[] = {srvHeap_};
  commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

  // 描画コマンドを発行
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

  // Viewport（マルチウィンドウ）の更新と描画
  ImGuiIO& io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault(nullptr, (void*)commandList);
  }

#endif // USE_IMGUI
}
