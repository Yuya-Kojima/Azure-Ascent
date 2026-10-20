#include "RoarDistortion.h"
#include "../../externals/nlohmann/json.hpp"
#include "Effect/EffectManager.h"
#include "Render/Renderer/PostProcess.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/RoarDistortionConfig.json";

// 1 フレームの秒（StageIntro の時計と同じ）
const float kFrameSec = 1.0f / 60.0f;
// 輪の長さのうち、強さが最大になるまでの割合
const float kRingAttackRatio = 0.08f;
// 同時に出せる輪の上限（PostProcess の上限）
const size_t kMaxRings = 5;

// 設定ファイルがないときの輪（吠え声 +0.15・+0.35・+0.55 秒に、だんだん弱く）
std::vector<RoarDistortion::Ring> MakeDefaultRings() {
  return {
      {0.15f, 0.65f, 1.15f, 0.08f, 0.035f, 0.3f},
      {0.35f, 0.65f, 1.15f, 0.07f, 0.025f, 0.3f},
      {0.55f, 0.60f, 1.15f, 0.06f, 0.015f, 0.3f},
  };
}
} // namespace

void RoarDistortion::Initialize() {
  config_ = {};
  config_.rings = MakeDefaultRings();
  LoadConfig();
  previewTime_.reset();
  isBlurActive_ = false;
}

float RoarDistortion::RingWeight(float u) {
  const float attack = (std::min)(u / kRingAttackRatio, 1.0f);
  const float remain = 1.0f - u;
  return attack * remain * remain;
}

float RoarDistortion::RingRadiusRatio(float u) {
  const float remain = 1.0f - u;
  return 1.0f - remain * remain;
}

float RoarDistortion::BlurStrength(float time) const {
  const Blur &blur = config_.blur;
  if (time < blur.riseStartTime || time >= blur.endTime) {
    return 0.0f;
  }
  if (time < blur.peakStartTime) {
    return (time - blur.riseStartTime) /
           (std::max)(blur.peakStartTime - blur.riseStartTime, kFrameSec);
  }
  if (time < blur.peakEndTime) {
    return 1.0f;
  }
  return 1.0f - (time - blur.peakEndTime) /
                    (std::max)(blur.endTime - blur.peakEndTime, kFrameSec);
}

void RoarDistortion::Update(std::optional<float> timeSinceRoar,
                            const Vector3 &headWorldPos, const ICamera *camera,
                            PostProcess *postProcess) {
  // 「ゆがみだけ再生」中は、その経過秒を吠え声からの秒として使う
  if (previewTime_) {
    timeSinceRoar = previewTime_;
    *previewTime_ += kFrameSec;

    float endTime = config_.blur.endTime;
    for (const Ring &ring : config_.rings) {
      endTime = (std::max)(endTime, ring.startTime + ring.duration);
    }
    if (*previewTime_ > endTime) {
      previewTime_.reset();
    }
  }

  float blurStrength = 0.0f;
  if (timeSinceRoar && camera) {
    const float time = *timeSinceRoar;

    // 今出ている輪を渡す
    for (const Ring &ring : config_.rings) {
      if (ring.duration <= 0.0f) {
        continue;
      }
      const float u = (time - ring.startTime) / ring.duration;
      if (u < 0.0f || u >= 1.0f) {
        continue;
      }
      EffectManager::GetInstance()->SubmitShockwave(
          headWorldPos, ring.maxRadius * RingRadiusRatio(u), ring.thickness,
          ring.distortion, RingWeight(u), ring.aberration);
    }

    blurStrength = BlurStrength(time);
  }

  if (!postProcess) {
    return;
  }

  // 放射状のぼかし（頭を中心に、外側だけ）
  if (blurStrength > 0.0f) {
    const Blur &blur = config_.blur;
    const Vector2 center =
        EffectManager::WorldToScreenUV(headWorldPos, camera);
    postProcess->SetUseRadialBlur(true);
    postProcess->SetRadialBlurCenter(center.x, center.y);
    postProcess->SetRadialBlurWidth(blur.width * blurStrength);
    postProcess->SetRadialBlurSamples(blur.samples);
    postProcess->SetRadialBlurInnerRadius(blur.innerRadius);
    postProcess->SetRadialBlurOuterRadius(blur.outerRadius);
    postProcess->SetRadialBlurAberration(blur.aberration);
    isBlurActive_ = true;
  } else if (isBlurActive_) {
    postProcess->SetUseRadialBlur(false);
    isBlurActive_ = false;
  }
}

void RoarDistortion::LoadConfig() {
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

  const auto rings = root.find("rings");
  if (rings != root.end() && rings->is_array()) {
    config_.rings.clear();
    for (const auto &item : *rings) {
      if (config_.rings.size() >= kMaxRings) {
        break;
      }
      Ring ring;
      ring.startTime = item.value("startTime", ring.startTime);
      ring.duration = item.value("duration", ring.duration);
      ring.maxRadius = item.value("maxRadius", ring.maxRadius);
      ring.thickness = item.value("thickness", ring.thickness);
      ring.distortion = item.value("distortion", ring.distortion);
      ring.aberration = item.value("aberration", ring.aberration);
      config_.rings.push_back(ring);
    }
  }

  const auto blurIt = root.find("blur");
  if (blurIt != root.end() && blurIt->is_object()) {
    Blur &blur = config_.blur;
    blur.riseStartTime = blurIt->value("riseStartTime", blur.riseStartTime);
    blur.peakStartTime = blurIt->value("peakStartTime", blur.peakStartTime);
    blur.peakEndTime = blurIt->value("peakEndTime", blur.peakEndTime);
    blur.endTime = blurIt->value("endTime", blur.endTime);
    blur.width = blurIt->value("width", blur.width);
    blur.samples = blurIt->value("samples", blur.samples);
    blur.innerRadius = blurIt->value("innerRadius", blur.innerRadius);
    blur.outerRadius = blurIt->value("outerRadius", blur.outerRadius);
    blur.aberration = blurIt->value("aberration", blur.aberration);
  }
}

void RoarDistortion::SaveConfig() const {
  nlohmann::json root;
  root["rings"] = nlohmann::json::array();
  for (const Ring &ring : config_.rings) {
    root["rings"].push_back({{"startTime", ring.startTime},
                             {"duration", ring.duration},
                             {"maxRadius", ring.maxRadius},
                             {"thickness", ring.thickness},
                             {"distortion", ring.distortion},
                             {"aberration", ring.aberration}});
  }
  const Blur &blur = config_.blur;
  root["blur"] = {{"riseStartTime", blur.riseStartTime},
                  {"peakStartTime", blur.peakStartTime},
                  {"peakEndTime", blur.peakEndTime},
                  {"endTime", blur.endTime},
                  {"width", blur.width},
                  {"samples", blur.samples},
                  {"innerRadius", blur.innerRadius},
                  {"outerRadius", blur.outerRadius},
                  {"aberration", blur.aberration}};

  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void RoarDistortion::DrawImGui() {
#ifdef USE_IMGUI
  if (!ImGui::TreeNode("Roar Distortion")) {
    return;
  }

  if (ImGui::Button((const char *)u8"ゆがみだけ再生")) {
    previewTime_ = 0.0f;
  }
  if (previewTime_) {
    ImGui::SameLine();
    ImGui::Text("%.2f s", *previewTime_);
  }

  ImGui::SeparatorText((const char *)u8"輪（時刻は吠え声からの秒、長さは画面の高さ = 1）");
  int removeIndex = -1;
  for (size_t i = 0; i < config_.rings.size(); ++i) {
    Ring &ring = config_.rings[i];
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::TreeNode("Ring", (const char *)u8"%zu 本目", i + 1)) {
      ImGui::DragFloat((const char *)u8"出る時刻", &ring.startTime, 0.01f, -1.0f, 3.0f);
      ImGui::DragFloat((const char *)u8"長さ", &ring.duration, 0.01f, 0.05f, 3.0f);
      ImGui::DragFloat((const char *)u8"最大の半径", &ring.maxRadius, 0.01f, 0.0f, 3.0f);
      ImGui::DragFloat((const char *)u8"太さ（半分）", &ring.thickness, 0.001f, 0.0f, 0.5f);
      ImGui::DragFloat((const char *)u8"押し曲げる量", &ring.distortion, 0.001f, 0.0f, 0.2f);
      ImGui::DragFloat((const char *)u8"色ずれ", &ring.aberration, 0.01f, 0.0f, 2.0f);
      if (ImGui::Button((const char *)u8"この輪を消す")) {
        removeIndex = static_cast<int>(i);
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (removeIndex >= 0) {
    config_.rings.erase(config_.rings.begin() + removeIndex);
  }
  if (config_.rings.size() < kMaxRings && ImGui::Button((const char *)u8"輪を足す")) {
    config_.rings.push_back(config_.rings.empty() ? Ring{} : config_.rings.back());
  }

  ImGui::SeparatorText((const char *)u8"放射状のぼかし（時刻は吠え声からの秒）");
  Blur &blur = config_.blur;
  ImGui::DragFloat((const char *)u8"強まり始め", &blur.riseStartTime, 0.01f, -1.0f, 3.0f);
  ImGui::DragFloat((const char *)u8"最大になる", &blur.peakStartTime, 0.01f, -1.0f, 3.0f);
  ImGui::DragFloat((const char *)u8"弱まり始め", &blur.peakEndTime, 0.01f, -1.0f, 3.0f);
  ImGui::DragFloat((const char *)u8"0 になる", &blur.endTime, 0.01f, -1.0f, 3.0f);
  ImGui::DragFloat((const char *)u8"ぼかしの幅", &blur.width, 0.0005f, 0.0f, 0.05f, "%.4f");
  ImGui::DragInt((const char *)u8"サンプル数", &blur.samples, 1.0f, 1, 32);
  ImGui::DragFloat((const char *)u8"内側の半径", &blur.innerRadius, 0.01f, 0.0f, 1.5f);
  ImGui::DragFloat((const char *)u8"外側の半径", &blur.outerRadius, 0.01f, 0.0f, 1.5f);
  ImGui::DragFloat((const char *)u8"色ずれ##blur", &blur.aberration, 0.01f, -0.5f, 0.5f);
  blur.outerRadius = (std::max)(blur.outerRadius, blur.innerRadius);

  if (ImGui::Button("Save Roar Distortion")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Roar Distortion")) {
    LoadConfig();
  }

  ImGui::TreePop();
#endif // USE_IMGUI
}
