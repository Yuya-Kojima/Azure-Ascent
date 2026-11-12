#include "Camera/SerpentCameraDirector.h"
#include "Camera/RailCamera.h"
#include "../../externals/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/SerpentCameraConfig.json";
} // namespace

void SerpentCameraDirector::Initialize() { LoadConfig(); }

void SerpentCameraDirector::OnEvent(MechSerpent::Event event, const MechSerpent &serpent,
                                    RailCamera &railCamera) {
  switch (event) {
  case MechSerpent::Event::Appeared:
    // 最後の見せ場なら、終わるまで顔の向きのキーを止める
    if (serpent.IsFinal()) {
      railCamera.HoldLook(climaxLookHoldT_);
    }
    break;
  case MechSerpent::Event::ChargeStarted:
    // 光り始めた合図に、光る節の真ん中を中心に拡大して、保って、戻す。最後の見せ場ならレールも遅くする
    railCamera.PunchZoom(serpent.GetCoreCenter(), serpent.IsPreview() ? previewPunchFovScale_ : punchFovScale_,
                         punchInSeconds_, punchHoldSeconds_, punchOutSeconds_);
    if (serpent.IsFinal()) {
      railCamera.ChangeEventSpeedScale(climaxSpeedScale_, climaxSlowSeconds_);
    }
    break;
  case MechSerpent::Event::Ended:
    if (serpent.IsFinal()) {
      railCamera.ChangeEventSpeedScale(1.0f, climaxRestoreSeconds_);
      railCamera.ReleaseLook(climaxLookReleaseSeconds_);
    }
    break;
  }
}

void SerpentCameraDirector::VisitConfigValues(
    const std::function<void(const std::string &key, float &value)> &visit) {
  visit("punchFovScale", punchFovScale_);
  visit("previewPunchFovScale", previewPunchFovScale_);
  visit("punchInSeconds", punchInSeconds_);
  visit("punchHoldSeconds", punchHoldSeconds_);
  visit("punchOutSeconds", punchOutSeconds_);
  visit("climaxSpeedScale", climaxSpeedScale_);
  visit("climaxSlowSeconds", climaxSlowSeconds_);
  visit("climaxRestoreSeconds", climaxRestoreSeconds_);
  visit("climaxLookHoldT", climaxLookHoldT_);
  visit("climaxLookReleaseSeconds", climaxLookReleaseSeconds_);
}

#ifdef USE_IMGUI
void SerpentCameraDirector::DrawImGui() {
  ImGui::SeparatorText("大蛇のカメラ");
  ImGui::PushID("SerpentCamera");
  ImGui::TextDisabled("光り始めの合図（画角を狭めて、保って、戻す）");
  ImGui::DragFloat("画角の倍率", &punchFovScale_, 0.01f, 0.2f, 1.0f, "%.2f");
  ImGui::DragFloat("画角の倍率（予告）", &previewPunchFovScale_, 0.01f, 0.2f, 1.0f, "%.2f");
  ImGui::DragFloat("押し込む秒", &punchInSeconds_, 0.01f, 0.0f, 2.0f, "%.2f");
  ImGui::DragFloat("保つ秒", &punchHoldSeconds_, 0.01f, 0.0f, 2.0f, "%.2f");
  ImGui::DragFloat("戻す秒", &punchOutSeconds_, 0.01f, 0.0f, 2.0f, "%.2f");
  ImGui::TextDisabled("最後の見せ場の間");
  ImGui::DragFloat("レールの速さの倍率", &climaxSpeedScale_, 0.01f, 0.0f, 1.0f, "%.2f");
  ImGui::DragFloat("遅くする秒", &climaxSlowSeconds_, 0.01f, 0.0f, 5.0f, "%.2f");
  ImGui::DragFloat("速さを戻す秒", &climaxRestoreSeconds_, 0.01f, 0.0f, 5.0f, "%.2f");
  ImGui::DragFloat("顔の向きを止める t", &climaxLookHoldT_, 0.01f, 0.0f, 0.0f, "%.2f");
  ImGui::DragFloat("顔の向きを戻す秒", &climaxLookReleaseSeconds_, 0.01f, 0.0f, 10.0f, "%.2f");
  if (ImGui::Button("大蛇のカメラを保存")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("読み直す")) {
    LoadConfig();
  }
  ImGui::PopID();
  ImGui::Spacing();
}
#endif

void SerpentCameraDirector::LoadConfig() {
  std::ifstream file(kConfigPath);
  if (!file.is_open()) {
    return;
  }
  nlohmann::json root;
  try {
    file >> root;
  } catch (const nlohmann::json::exception &) {
    return;
  }
  // ファイルにないキーは今の値のまま
  VisitConfigValues([&](const std::string &key, float &value) {
    if (root.contains(key) && root[key].is_number()) {
      value = root[key].get<float>();
    }
  });
}

void SerpentCameraDirector::SaveConfig() {
  nlohmann::json root;
  VisitConfigValues([&](const std::string &key, float &value) { root[key] = value; });
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}
