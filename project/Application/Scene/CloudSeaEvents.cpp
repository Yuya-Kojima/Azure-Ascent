#include "CloudSeaEvents.h"
#include "../../externals/nlohmann/json.hpp"
#include "Render/CloudSea/CloudSea.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/CloudSeaConfig.json";

void ReadFloat(const nlohmann::json &root, const char *key, float &value) {
  const auto it = root.find(key);
  if (it != root.end() && it->is_number()) {
    value = it->get<float>();
  }
}

void ReadVector2(const nlohmann::json &root, const char *key, Vector2 &value) {
  const auto it = root.find(key);
  if (it != root.end() && it->is_array() && it->size() == 2) {
    value = {(*it)[0].get<float>(), (*it)[1].get<float>()};
  }
}

void ReadVector3(const nlohmann::json &root, const char *key, Vector3 &value) {
  const auto it = root.find(key);
  if (it != root.end() && it->is_array() && it->size() == 3) {
    value = {(*it)[0].get<float>(), (*it)[1].get<float>(),
             (*it)[2].get<float>()};
  }
}
} // namespace

void CloudSeaEvents::Initialize() { LoadConfig(); }

void CloudSeaEvents::Add(const Event &event) { events_.push_back(event); }

void CloudSeaEvents::Clear() { events_.clear(); }

void CloudSeaEvents::SetTime(float time) {
  time_ = time;
  // 閉じ終わった出来事を消す
  std::erase_if(events_, [time](const Event &e) {
    return time > e.startTime + e.riseSec + e.holdSec + e.fallSec;
  });
}

float CloudSeaEvents::ApplyEase(Ease ease, float t) {
  switch (ease) {
  case Ease::In:
    return EaseInQuad(t);
  case Ease::Out:
    return EaseOutQuad(t);
  case Ease::InOut:
    return EaseInOutQuad(t);
  default:
    return t;
  }
}

float CloudSeaEvents::CalcStrength(const Event &event, float time) {
  const float local = time - event.startTime;
  if (local < 0.0f) {
    return -1.0f;
  }
  // 開く：0 → 1
  float strength =
      event.riseSec > 0.0f
          ? ApplyEase(event.riseEase,
                      std::clamp(local / event.riseSec, 0.0f, 1.0f))
          : 1.0f;
  // 閉じる：1 → 0
  const float fallStart = event.riseSec + event.holdSec;
  if (local > fallStart) {
    const float t =
        event.fallSec > 0.0f
            ? std::clamp((local - fallStart) / event.fallSec, 0.0f, 1.0f)
            : 1.0f;
    strength *= 1.0f - ApplyEase(event.fallEase, t);
  }
  return strength;
}

void CloudSeaEvents::Apply(CloudSea *cloudSea) const {
  if (!cloudSea) {
    return;
  }
  const Config &c = config_;
  cloudSea->SetHeight(c.height);
  cloudSea->SetRelief(c.amplitude, c.blobSize);
  cloudSea->SetWind(c.wind);
  CloudSeaLook look;
  look.thickness = c.thickness;
  look.edgeSoftness = c.edgeSoftness;
  look.density = c.density;
  look.color = c.color;
  look.brightness = c.brightness;
  look.powder = c.powder;
  look.valleyDarkness = c.valleyDarkness;
  look.forwardScatter = c.forwardScatter;
  look.rimStrength = c.rimStrength;
  look.rimPower = c.rimPower;
  look.noiseSize = c.noiseSize;
  look.detailSize = c.detailSize;
  look.detailStrength = c.detailStrength;
  look.jitter = c.jitter;
  cloudSea->SetLook(look);
  cloudSea->SetDebugView(CloudSea::DebugView(debugView_));
  cloudSea->ClearShapes();

  int shapeIndex = 0;
  for (const Event &e : events_) {
    const float strength = CalcStrength(e, time_);
    // まだ始まっていない
    if (strength < 0.0f) {
      continue;
    }
    if (shapeIndex >= CloudSea::kMaxShapes) {
      break;
    }
    CloudSeaShape shape;
    shape.center = {e.position.x, e.position.z};
    shape.radius = e.size;
    if (e.kind == Kind::Swell) {
      shape.kind = CloudSeaShape::Kind::Swell;
      shape.amount = e.amount * strength;
      // ひびは盛り上がりが半分を超えてから入る
      shape.crack = e.crack * std::clamp(strength * 2.0f - 1.0f, 0.0f, 1.0f);
    } else {
      shape.kind = CloudSeaShape::Kind::Tear;
      shape.width = e.amount;
      shape.amount = strength;
    }
    cloudSea->SetShape(shapeIndex++, shape);
  }
}

void CloudSeaEvents::LoadConfig() {
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
  Config &c = config_;
  ReadFloat(root, "height", c.height);
  ReadFloat(root, "amplitude", c.amplitude);
  ReadFloat(root, "blobSize", c.blobSize);
  ReadVector2(root, "wind", c.wind);
  ReadFloat(root, "thickness", c.thickness);
  ReadFloat(root, "edgeSoftness", c.edgeSoftness);
  ReadFloat(root, "density", c.density);
  ReadVector3(root, "color", c.color);
  ReadFloat(root, "brightness", c.brightness);
  ReadFloat(root, "powder", c.powder);
  ReadFloat(root, "valleyDarkness", c.valleyDarkness);
  ReadFloat(root, "forwardScatter", c.forwardScatter);
  ReadFloat(root, "rimStrength", c.rimStrength);
  ReadFloat(root, "rimPower", c.rimPower);
  ReadFloat(root, "noiseSize", c.noiseSize);
  ReadFloat(root, "detailSize", c.detailSize);
  ReadFloat(root, "detailStrength", c.detailStrength);
  ReadFloat(root, "jitter", c.jitter);
}

void CloudSeaEvents::SaveConfig() const {
  const Config &c = config_;
  nlohmann::json root;
  root["height"] = c.height;
  root["amplitude"] = c.amplitude;
  root["blobSize"] = c.blobSize;
  root["wind"] = {c.wind.x, c.wind.y};
  root["thickness"] = c.thickness;
  root["edgeSoftness"] = c.edgeSoftness;
  root["density"] = c.density;
  root["color"] = {c.color.x, c.color.y, c.color.z};
  root["brightness"] = c.brightness;
  root["powder"] = c.powder;
  root["valleyDarkness"] = c.valleyDarkness;
  root["forwardScatter"] = c.forwardScatter;
  root["rimStrength"] = c.rimStrength;
  root["rimPower"] = c.rimPower;
  root["noiseSize"] = c.noiseSize;
  root["detailSize"] = c.detailSize;
  root["detailStrength"] = c.detailStrength;
  root["jitter"] = c.jitter;

  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void CloudSeaEvents::DrawImGui() {
#ifdef USE_IMGUI
  Config &c = config_;
  if (ImGui::TreeNode("Sea Look")) {
    ImGui::SeparatorText("Shape");
    ImGui::DragFloat("Height", &c.height, 0.1f);
    ImGui::SliderFloat("Amplitude", &c.amplitude, 0.0f, 8.0f);
    ImGui::SliderFloat("Blob Size", &c.blobSize, 5.0f, 100.0f);
    ImGui::SliderFloat("Thickness", &c.thickness, 2.0f, 60.0f);
    ImGui::SliderFloat("Edge Softness", &c.edgeSoftness, 0.1f, 4.0f);
    ImGui::SliderFloat("Density", &c.density, 0.1f, 8.0f);
    ImGui::SliderFloat("Noise Size", &c.noiseSize, 5.0f, 200.0f);
    ImGui::SliderFloat("Detail Size", &c.detailSize, 1.0f, 30.0f);
    ImGui::SliderFloat("Detail Strength", &c.detailStrength, 0.0f, 0.9f);
    ImGui::DragFloat2("Wind (X, Z)", &c.wind.x, 0.05f, -10.0f, 10.0f);
    ImGui::SeparatorText("Light");
    ImGui::ColorEdit3("Color", &c.color.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::SliderFloat("Brightness", &c.brightness, 0.0f, 2.0f);
    ImGui::SliderFloat("Powder", &c.powder, 0.0f, 1.0f);
    ImGui::SliderFloat("Valley Darkness", &c.valleyDarkness, 0.0f, 1.0f);
    ImGui::SliderFloat("Forward Scatter", &c.forwardScatter, 0.0f, 1.0f);
    ImGui::SliderFloat("Rim Strength", &c.rimStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Rim Power", &c.rimPower, 1.0f, 8.0f);
    ImGui::SeparatorText("Debug");
    ImGui::SliderFloat("Jitter", &c.jitter, 0.0f, 1.0f);
    const char *const kDebugViews[] = {"Normal", "Sun Only", "Ambient Only",
                                       "Density Only", "Step Count"};
    ImGui::Combo("View", &debugView_, kDebugViews, IM_ARRAYSIZE(kDebugViews));
    ImGui::TreePop();
  }
  ImGui::Text("Event Time %.2f  Events %d", time_, int(events_.size()));
  if (ImGui::Button("Save Cloud Sea Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Cloud Sea Config")) {
    LoadConfig();
  }
#endif // USE_IMGUI
}
