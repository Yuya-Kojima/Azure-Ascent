#include "Core/Game.h"
#include "Debug/Logger.h"
#include <filesystem>
#include <iterator>
#include "Math/Vector2.h"
#include "Collision/CollisionManager.h"
#include "Core/ResourceObject.h"
#include "Core/SrvManager.h"
#include "Effect/EffectManager.h"
#include "Effect/FractureCache.h"
#include "Framework/ActorManager.h"
#include "Framework/UIManager.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Render/Camera/ICamera.h"
#include "Renderer/ModelRenderer.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/PostProcess.h"
#include "Renderer/SpriteRenderer.h"
#include "Scene/SceneFactory.h"
#include "Scene/GamePlayScene.h"
#include "Scene/SceneManager.h"
#include "Framework/GameManager.h"
#include "Sprite/Sprite.h"
#include "Texture/TextureManager.h"
#include <cassert>
#include <fstream>
#ifdef USE_IMGUI
#include "ImGuizmo.h"
#endif

void Game::Initialize() {

  // 基底クラスの初期化処理
  EngineBase::Initialize();

  // ステージで使う重い準備（BGM・効果音の読み込み、敵のモデルを割る、エフェクトの粒の入れ物を作る）は起動時に済ませる
  // BGM は mp3 を丸ごと PCM に変換するため 1 秒ほどかかり、シーン切り替えの途中で読むと画面が止まる
  GamePlayScene::PreloadResources(this);
  EffectManager::GetInstance()->Initialize();

  SceneManager::GetInstance()->Initialize(this);

  sceneFactory_ = std::make_unique<SceneFactory>();

  SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());
  // 初期シーンの設定
  SceneManager::GetInstance()->ChangeScene("TITLE");

  //===========================
  // ローカル変数宣言
  //===========================

  //===========================
  // ImGuiManagerの初期化
  //===========================
  imGuiManager_ = std::make_unique<ImGuiManager>();
  imGuiManager_->Initialize(windowSystem_.get(), dx12Core_.get(),
                            srvManager_.get());

  //===========================
  // RenderPipelineの初期化
  //===========================
  renderPipeline_ = std::make_unique<RenderPipeline>();
  renderPipeline_->Initialize(dx12Core_.get(), srvManager_.get(),
                              tiledLightCuller_.get());

  scene3DGpuTimer_.Initialize(dx12Core_.get());
  
#ifdef USE_IMGUI
  // ImGuiが有効な場合のみエディタモード（オフスクリーン描画）を有効化
  renderPipeline_->SetEditorMode(true);
#else
  // Release時などImGuiが無効な場合は直接バックバッファに描画
  renderPipeline_->SetEditorMode(false);
#endif

  // texture切り替え用
  bool useMonsterBall = true;

  // Lighting
  bool enableLighting = true;

  // デルタタイム 一旦60fpsで固定
  const float kDeltaTime = 1.0f / 60.0f;

  bool isUpdate = false;

  bool useBillboard = true;

  //// 風を出す範囲 ※今は使用していないのでコメントアウト
  // AccelerationField accelerationField;
  // accelerationField.acceleration = {15.0f, 0.0f, 0.0f};
  // accelerationField.area.min = {-1.0f, -1.0f, -1.0f};
  // accelerationField.area.max = {1.0f, 1.0f, 1.0f};
}

#ifdef USE_IMGUI
static bool g_showUIEditor = false;

static void DrawProjectDirectoryTree(const std::filesystem::path& dirPath) {
  for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
    if (entry.is_directory()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f)); // Folder color
      bool isOpen = ImGui::TreeNodeEx(entry.path().filename().string().c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
      ImGui::PopStyleColor();
      if (isOpen) {
        DrawProjectDirectoryTree(entry.path());
        ImGui::TreePop();
      }
    } else if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();
      std::string ext = entry.path().extension().string();
      std::string relativePath = entry.path().string();
      std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

      ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      std::string icon = "  ";
      
      if (ext == ".obj" || ext == ".gltf") {
        color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // Green
        icon = "[M] ";
      } else if (ext == ".png" || ext == ".dds") {
        color = ImVec4(0.4f, 0.8f, 1.0f, 1.0f); // Cyan
        icon = "[T] ";
      } else if (ext == ".mtl" || ext == ".json" || ext == ".bin") {
        color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
        icon = "[D] ";
      } else if (ext == ".wav" || ext == ".mp3") {
        color = ImVec4(1.0f, 0.6f, 0.8f, 1.0f); // Pink
        icon = "[A] ";
      }

      std::string displayStr = icon + filename;
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      if (ImGui::Selectable(displayStr.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0) && ext == ".json") {
          g_showUIEditor = true;
          UIManager::GetInstance()->Load(relativePath);
        }
      }
      ImGui::PopStyleColor();

      if (ext == ".obj" || ext == ".gltf") {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
          ImGui::SetDragDropPayload("DND_MODEL_PATH", relativePath.c_str(), relativePath.size() + 1);
          ImGui::Text("Deploy %s", filename.c_str());
          ImGui::EndDragDropSource();
        }
      }
    }
  }
}

static void DrawPaletteDirectoryTree(const std::filesystem::path& dirPath) {
  for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
    if (entry.is_directory()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f)); // Folder color
      bool isOpen = ImGui::TreeNodeEx(entry.path().filename().string().c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
      ImGui::PopStyleColor();
      if (isOpen) {
        DrawPaletteDirectoryTree(entry.path());
        ImGui::TreePop();
      }
    } else if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();
      std::string ext = entry.path().extension().string();
      std::string relativePath = entry.path().string();
      std::replace(relativePath.begin(), relativePath.end(), '\\', '/');

      // Paletteにはモデル(.obj, .gltf)とPrefab(.json)のみ表示
      if (ext != ".obj" && ext != ".gltf" && ext != ".json") {
          continue;
      }

      ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      std::string icon = "  ";
      
      if (ext == ".obj" || ext == ".gltf") {
        color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); // Green
        icon = "[M] ";
      } else if (ext == ".json") {
        color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f); // Gray
        icon = "[P] "; // Prefab
      }

      std::string displayStr = icon + filename;
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::Selectable(displayStr.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
      ImGui::PopStyleColor();

      if (ext == ".obj" || ext == ".gltf" || ext == ".json") {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
          ImGui::SetDragDropPayload("DND_MODEL_PATH", relativePath.c_str(), relativePath.size() + 1);
          ImGui::Text("Deploy %s", filename.c_str());
          ImGui::EndDragDropSource();
        }
      }
    }
  }
}
#endif

void Game::Finalize() {

  imGuiManager_->Finalize();

  // シーンの解放
  SceneManager::GetInstance()->Finalize();

  // マネージャーのメモリ解放
  ActorManager::GetInstance()->Clear();
  CollisionManager::GetInstance()->Clear();

  // 破片の形セットは GPU バッファを持つので、デバイスを消す EngineBase::Finalize より前に解放する
  // （形セットを借りている爆発は、上の ActorManager::Clear で先に消えている）
  FractureCache::GetInstance()->Clear();
  // エフェクトの粒の入れ物も GPU バッファを持つので、同じくここで解放する
  EffectManager::GetInstance()->Finalize();

  renderPipeline_.reset();
  // GPU の時刻を受け取るバッファを、デバイスより先に解放する
  scene3DGpuTimer_ = GpuTimer{};
  imGuiManager_.reset();
  sceneFactory_.reset();

  // 基底クラスの終了処理
  EngineBase::Finalize();
}

void Game::Update() {

  // 基底クラスの更新処理
  EngineBase::Update();
  if (IsEndRequest()) {
    return;
  }

#ifdef USE_IMGUI

  // ImGui受付開始
  if (imGuiManager_) {
    imGuiManager_->Begin();
    ImGuizmo::BeginFrame();
  }

#endif // USE_IMGUI

  SceneManager::GetInstance()->Update();

  // FPSをセット
  dx12Core_->SetFPS(set60FPS_);

#ifdef USE_IMGUI

  // =====================================
  // Main Toolbar
  // =====================================
  ImGui::Begin("Main Toolbar");
    
    // Play / Stop Buttons
    bool isPlayMode = GameManager::GetInstance()->IsGlobalPlayMode();
        if (!isPlayMode) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        if (ImGui::Button(" [ > PLAY ] ")) {
          GameManager::GetInstance()->SetPlayStartSceneName(SceneManager::GetInstance()->GetCurrentSceneName());
          GameManager::GetInstance()->SetPlayStartLevelName(GameManager::GetInstance()->GetCurrentLevel());
          GameManager::GetInstance()->SetGlobalPlayMode(true);
        }
        ImGui::PopStyleColor();
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button(" [ ■ STOP ] ")) {
          GameManager::GetInstance()->SetGlobalPlayMode(false);
          
          // Play開始時のシーンに戻る（ただし同じシーンの場合は何もしない）
          std::string startScene = GameManager::GetInstance()->GetPlayStartSceneName();
          if (SceneManager::GetInstance()->GetCurrentSceneName() != startScene) {
              GameManager::GetInstance()->SetCurrentLevel(GameManager::GetInstance()->GetPlayStartLevelName());
              SceneManager::GetInstance()->SetNextTransitionFade(0.0f); // フェードなしで瞬時に戻る
              SceneManager::GetInstance()->ChangeScene(startScene);
          }
        }
        ImGui::PopStyleColor();
      }
    
    ImGui::End();

    // =====================================
    // Main Menu Bar
    // =====================================
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Scene")) {
        if (ImGui::MenuItem("Title")) {
          SceneManager::GetInstance()->ChangeScene("TITLE");
        }
        if (ImGui::MenuItem("Stage Select")) {
          SceneManager::GetInstance()->ChangeScene("STAGE_SELECT");
        }
        if (ImGui::BeginMenu("Gameplay")) {
          std::string levelsPath = "resources/levels";
          if (std::filesystem::exists(levelsPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(levelsPath)) {
              if (entry.path().extension() == ".json") {
                std::string fileName = entry.path().filename().string();
                if (ImGui::MenuItem(fileName.c_str())) {
                  GameManager::GetInstance()->SetCurrentLevel(fileName);
                  SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
                }
              }
            }
          }
          ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Debug")) {
          SceneManager::GetInstance()->ChangeScene("DEBUG");
        }
        if (ImGui::MenuItem("Fracture Test")) {
          SceneManager::GetInstance()->ChangeScene("FRACTURE_TEST");
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Window")) {
      ImGui::MenuItem("UI Editor", NULL, &g_showUIEditor);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (g_showUIEditor) {
    UIManager::GetInstance()->DrawEditor();
  }

  // =====================================
  // Console
  // =====================================
  ImGui::Begin("Console");
  if (ImGui::Button("Clear")) {
    Logger::ClearConsoleLog();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy All")) {
    ImGui::SetClipboardText(Logger::GetConsoleLog().c_str());
  }
  ImGui::Separator();
  ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
  std::string logs = Logger::GetConsoleLog();
  
  // テキスト選択は不可だが高速に表示できるTextUnformattedを使用
  ImGui::TextUnformatted(logs.c_str(), logs.c_str() + logs.size());

  // ログのサイズが変わった（新しいログが追加された）かどうかを判定
  static size_t s_prevLogSize = 0;
  bool isUpdated = (logs.size() != s_prevLogSize);
  s_prevLogSize = logs.size();

  // 更新された直後、またはすでに一番下を見ている場合は一番下へ自動スクロール
  if (isUpdated || ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();
  ImGui::End();

  // =====================================
  // Project
  // =====================================
  ImGui::Begin("Project");
  if (std::filesystem::exists("resources")) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
    bool isOpen = ImGui::TreeNodeEx("resources", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);
    ImGui::PopStyleColor();
    if (isOpen) {
      DrawProjectDirectoryTree("resources");
      ImGui::TreePop();
    }
  }
  ImGui::End();

  // =====================================
  // Palette (Level Editor Models)
  // =====================================
  ImGui::Begin("Palette");
  if (std::filesystem::exists("resources")) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.9f, 0.4f, 1.0f));
    bool isOpen = ImGui::TreeNodeEx("resources", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow);
    ImGui::PopStyleColor();
    if (isOpen) {
      DrawPaletteDirectoryTree("resources");
      ImGui::TreePop();
    }
  }
  ImGui::End();

  // =====================================
  // World Settings
  // =====================================
  DrawWorldSettingsUI();

  // エディタ用：ゲーム画面のプレビューウィンドウ
  ImGui::Begin("Game View");
  uint32_t srvIndex = renderPipeline_->GetEditorGameViewSrvIndex();
  D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = srvManager_->GetGPUDescriptorHandle(srvIndex);
  // ゲーム画面のアスペクト比に合わせて描画 (例: 1280x720)
  ImVec2 windowSize = ImGui::GetContentRegionAvail();
  float aspect = 1280.0f / 720.0f;
  float renderWidth = windowSize.x;
  float renderHeight = windowSize.x / aspect;
  if (renderHeight > windowSize.y) {
    renderHeight = windowSize.y;
    renderWidth = windowSize.y * aspect;
  }
  ImVec2 screenPos = ImGui::GetCursorScreenPos();
  ImGui::Image((ImTextureID)gpuHandle.ptr, ImVec2(renderWidth, renderHeight));

  if (g_showUIEditor) {
    UIManager::GetInstance()->DrawEditorGizmo(screenPos, ImVec2(renderWidth, renderHeight));
  }

  bool isHoveringGameView = false;
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MODEL_PATH", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
      isHoveringGameView = true;
      std::string path(static_cast<const char*>(payload->Data));
      if (auto scene = SceneManager::GetInstance()->GetCurrentScene()) {
        ImVec2 mousePos = ImGui::GetMousePos();
        float relativeX = mousePos.x - screenPos.x;
        float relativeY = mousePos.y - screenPos.y;
        
        Vector2 ndcPos{0.0f, 0.0f};
        // 画面内へのドロップかチェック
        if (relativeX >= 0.0f && relativeX <= renderWidth && relativeY >= 0.0f && relativeY <= renderHeight) {
            ndcPos.x = (relativeX / renderWidth) * 2.0f - 1.0f;
            ndcPos.y = 1.0f - (relativeY / renderHeight) * 2.0f; // Y軸は上向き正
        }
        
        if (payload->IsDelivery()) {
            scene->OnFileDropped(path, ndcPos);
        } else {
            scene->OnDragHovering(path, ndcPos);
        }
      }
    }
    ImGui::EndDragDropTarget();
  }

  // ドラッグ中ではない、またはGame View外に出た場合はプレビューを消す
  if (!isHoveringGameView) {
      if (auto scene = SceneManager::GetInstance()->GetCurrentScene()) {
          scene->OnDragHoverEnd();
      }
  }

  // GameView上に重ねてエディタ用UIを描画
  SceneManager::GetInstance()->DrawEditorUI();

  ImGui::End();

  // ImGui受付終了
  if (imGuiManager_) {
    imGuiManager_->End();
  }

#endif // USE_IMGUI
}

void Game::Draw() {

  EngineBase::BeginFrame();

  // HDRキャンバスの準備をして、3D空間を描画。
  // 命令の列は A（ここまで）→ P（深度だけの描画とマス分け）→ B（3D の描画から後）の順に GPU で動く
  renderPipeline_->Begin3DPass(dx12Core_.get());
  // 測り始めは P の始め。深度だけの描画・マス分け・3D の描画を全部測る
  scene3DGpuTimer_.Begin(dx12Core_->GetPrepassCommandList());
  // Object3d は B と P の両方に積む
  SceneManager::GetInstance()->Draw();
  renderPipeline_->End3DPass(object3dRenderer_->GetDefaultCamera());
  scene3DGpuTimer_.End();

  // プロジェクション逆行列のセット（PostProcessが必要とするため）
  auto pp = SceneManager::GetInstance()->GetCurrentScenePostProcess();
  if (pp) {
    const ICamera *defaultCamera = object3dRenderer_->GetDefaultCamera();
    if (defaultCamera) {
      pp->SetProjectionInverse(Inverse(defaultCamera->GetProjectionMatrix()));
    }
  }

  // ポストプロセスを計算し、メイン画面に焼き付ける
  renderPipeline_->DrawPostProcess(dx12Core_.get(), srvManager_.get(), pp);

  // 2Dオーバーレイ描画パス（ポストプロセスの後に上書き描画する）
  SceneManager::GetInstance()->DrawOverlay();

  // エディタ表示中はUIManagerを強制描画
#ifdef USE_IMGUI
  if (g_showUIEditor) {
    UIManager::GetInstance()->Draw();
  }
#endif

  // エディタテクスチャをSRVに変換し、ターゲットをバックバッファに戻す
  renderPipeline_->EndEditorGameViewPass(dx12Core_.get());

  // 2D
  // EngineBase::Begin2D();

  if (imGuiManager_) {
    imGuiManager_->Draw();
  }

  EngineBase::EndFrame();
}

void Game::DrawWorldSettingsUI() {
#ifdef USE_IMGUI
  ImGui::Begin("World Settings");
  
  // SRV ヒープの番号の使用数・これまでの最大・上限
  ImGui::Text("SRV  Used %u / Peak %u / Capacity %u",
              srvManager_->GetUsedCount(), srvManager_->GetPeakCount(),
              srvManager_->GetCapacity());

  // 3D の描画に GPU でかかった時間（前のフレームまでの値を慣らしたもの）
  ImGui::Text("GPU 3D %.3f ms", scene3DGpuTimer_.GetMilliseconds());

  // 直近の区切り（GpuTimer::GetWindowSampleCount() フレーム分）の平均・最小・最大
  const GpuTimer::WindowStats gpu3DStats = scene3DGpuTimer_.GetWindowStats();
  if (gpu3DStats.isValid) {
    ImGui::Text("  %u frames: avg %.3f / min %.3f / max %.3f ms",
                GpuTimer::GetWindowSampleCount(), gpu3DStats.average,
                gpu3DStats.min, gpu3DStats.max);
  } else {
    ImGui::Text("  %u frames: measuring...", GpuTimer::GetWindowSampleCount());
  }

  if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto* renderer = GetObject3dRenderer();
    if (renderer) {
      static bool showDirectionalLight = true;
      static float directionalIntensityBackup = 1.0f;

      // Checkboxes
      if (ImGui::Checkbox("Enable DirectionalLight", &showDirectionalLight)) {
        if (auto *dl = renderer->GetDirectionalLightData()) {
          if (!showDirectionalLight) {
            directionalIntensityBackup = dl->intensity;
            dl->intensity = 0.0f;
          } else {
            dl->intensity = (directionalIntensityBackup > 0.0f) ? directionalIntensityBackup : 1.0f;
          }
        }
      }
      
      // 調べるための表示（Object3d の描画を切り替える）
      if (auto *dl = renderer->GetDirectionalLightData()) {
        const char *const kDebugViewNames[] = {"Normal", "Albedo Only", "Normal Color",
                                               "Normal Facing", "Lighting Only",
                                               "Problem Pixels", "Tile Light Count"};
        ImGui::Combo("Debug View", &dl->debugView, kDebugViewNames,
                     static_cast<int>(std::size(kDebugViewNames)));
      }

      ImGui::Separator();

      if (showDirectionalLight) {
        if (auto* dl = renderer->GetDirectionalLightData()) {
          ImGui::Text("Directional Light");
          ImGui::ColorEdit3("Color##DL", &dl->color.x);
          ImGui::DragFloat("Intensity##DL", &dl->intensity, 0.01f, 0.0f, 10.0f);
          ImGui::Separator();
        }
      }
      // 点の光・スポットライトの名簿（光る物が毎フレーム書き込む）
      if (LightManager *lightManager = GetLightManager()) {
        lightManager->DrawImGui();
      }
    }
  }

  ImGui::End();
#endif
}

