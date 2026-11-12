#pragma once
#include "Core/EngineBase.h"
#include "Debug/GpuTimer.h"
#include "Debug/ImGuiManager.h"
#include "Math/MathUtil.h"
#include "Scene/BaseScene.h"
#include "Renderer/PostProcess.h"
#include "Render/Texture/RenderTexture.h"
#include "Render/Renderer/Bloom.h"
#include "Render/Renderer/RenderPipeline.h"
#include <memory>

class ImGuiManager;

class Game : public EngineBase {

public:
  void Initialize() override;

  void Finalize() override;

  void Update() override;

  void Draw() override;

private:
  void DrawWorldSettingsUI();
  bool set60FPS_ = true;

  std::unique_ptr<ImGuiManager> imGuiManager_ = nullptr;

  std::unique_ptr<RenderPipeline> renderPipeline_ = nullptr;

  // 3D の描画（シーンの Draw の全体）に GPU でかかった時間を測る
  GpuTimer scene3DGpuTimer_;
};
