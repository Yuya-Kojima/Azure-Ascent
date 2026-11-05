#include "StageLighting.h"
#include "../../externals/nlohmann/json.hpp"
#include "Math/MathUtil.h"
#include "Render/Renderer/Fog.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Render/Renderer/PostProcess.h"
#include "Render/SkyBox/SkyBox.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/StageLightingConfig.json";

// 3 成分の値の種類（ImGui の出し方を分ける）
enum class Vec3Kind { Color, Direction };

struct Vec3Item {
  const char *key;
  Vector3 StageLighting::Config::*member;
  Vec3Kind kind;
};

struct FloatItem {
  const char *key;
  float StageLighting::Config::*member;
  float min;
  float max;
};

struct BoolItem {
  const char *key;
  bool StageLighting::Config::*member;
};

// ImGui の見出しごとにまとめた項目
struct Section {
  const char *title;
  const Vec3Item *vec3Items;
  size_t vec3Count;
  const FloatItem *floatItems;
  size_t floatCount;
  const BoolItem *boolItems;
  size_t boolCount;
};

using C = StageLighting::Config;

const Vec3Item kSkyVec3[] = {
    {"skyZenith", &C::skyZenith, Vec3Kind::Color},
    {"skyHorizon", &C::skyHorizon, Vec3Kind::Color},
    {"sunDir", &C::sunDir, Vec3Kind::Direction},
    {"sunColor", &C::sunColor, Vec3Kind::Color},
    {"cloudColor", &C::cloudColor, Vec3Kind::Color},
};
const FloatItem kSkyFloat[] = {
    {"sunIntensity", &C::sunIntensity, 0.0f, 4.0f},
    {"horizonGlow", &C::horizonGlow, 0.0f, 10.0f},
    {"horizonGlowHeight", &C::horizonGlowHeight, 0.005f, 0.3f},
    {"horizonGlowAzimuth", &C::horizonGlowAzimuth, 0.5f, 40.0f},
    {"cloudAmount", &C::cloudAmount, 0.0f, 1.0f},
    {"cloudOpacity", &C::cloudOpacity, 0.0f, 1.0f},
    {"cloudRim", &C::cloudRim, 0.0f, 2.0f},
};

const Vec3Item kLightVec3[] = {
    {"lightFrom", &C::lightFrom, Vec3Kind::Direction},
    {"lightColor", &C::lightColor, Vec3Kind::Color},
    {"skyAmbient", &C::skyAmbient, Vec3Kind::Color},
    {"groundAmbient", &C::groundAmbient, Vec3Kind::Color},
    {"backRimFrom", &C::backRimFrom, Vec3Kind::Direction},
    {"backRimColor", &C::backRimColor, Vec3Kind::Color},
};
const FloatItem kLightFloat[] = {
    {"lightIntensity", &C::lightIntensity, 0.0f, 3.0f},
    {"backRimIntensity", &C::backRimIntensity, 0.0f, 5.0f},
    {"backRimPower", &C::backRimPower, 0.5f, 8.0f},
};

const FloatItem kFogFloat[] = {
    {"fogNear", &C::fogNear, 0.0f, 1000.0f},
    {"fogFar", &C::fogFar, 1.0f, 3000.0f},
};

const FloatItem kPostFloat[] = {
    {"exposure", &C::exposure, 0.1f, 3.0f},
    {"bloomThreshold", &C::bloomThreshold, 0.0f, 5.0f},
    {"bloomIntensity", &C::bloomIntensity, 0.0f, 3.0f},
    {"bloomSigma", &C::bloomSigma, 0.1f, 30.0f},
    {"vignetteScale", &C::vignetteScale, 1.0f, 40.0f},
    {"vignetteExponent", &C::vignetteExponent, 0.05f, 2.0f},
};
const BoolItem kPostBool[] = {
    {"useBloom", &C::useBloom},
    {"useVignette", &C::useVignette},
};

const Section kSections[] = {
    {"Sky", kSkyVec3, std::size(kSkyVec3), kSkyFloat, std::size(kSkyFloat),
     nullptr, 0},
    {"Light", kLightVec3, std::size(kLightVec3), kLightFloat,
     std::size(kLightFloat), nullptr, 0},
    {"Fog", nullptr, 0, kFogFloat, std::size(kFogFloat), nullptr, 0},
    {"Post Effect", nullptr, 0, kPostFloat, std::size(kPostFloat), kPostBool,
     std::size(kPostBool)},
};

Vector4 ToVector4(const Vector3 &v, float w) { return {v.x, v.y, v.z, w}; }
} // namespace

void StageLighting::Initialize() { LoadConfig(); }

void StageLighting::Apply(Skybox *skybox, Object3dRenderer *renderer,
                          PostProcess *postProcess, float skyTime) const {
  const Config &c = config_;

  // 空：空の色・太陽・雲・地平線の光
  if (skybox) {
    skybox->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    skybox->SetSkyColors(c.skyZenith, c.skyHorizon);
    skybox->SetSun(Normalize(c.sunDir), c.sunColor, c.sunIntensity,
                   c.cloudAmount);
    skybox->SetCloudColor(c.cloudColor, c.cloudOpacity, c.cloudRim);
    skybox->SetHorizonGlow(c.horizonGlow, c.horizonGlowHeight,
                           c.horizonGlowAzimuth);
    skybox->SetSkyTime(skyTime);
  }

  if (renderer) {
    // 平行光源：全シーン共通なので、このシーンの値で毎フレーム上書きする
    if (auto *dl = renderer->GetDirectionalLightData()) {
      dl->color = ToVector4(c.lightColor, 1.0f);
      // シェーダーは「光が進む向き」を使うので、光が来る方向の逆にする
      dl->direction = Normalize(c.lightFrom) * -1.0f;
      dl->intensity = c.lightIntensity;
      dl->skyAmbient = ToVector4(c.skyAmbient, 0.0f);
      dl->groundAmbient = ToVector4(c.groundAmbient, 0.0f);
      dl->backRimColor = ToVector4(c.backRimColor, c.backRimIntensity);
      dl->backRimPower = c.backRimPower;
      dl->backRimDirection = Normalize(c.backRimFrom) * -1.0f;
    }

    // 霧：遠くを地平線の色に溶かす
    FogData fog;
    fog.color = ToVector4(c.skyHorizon, 1.0f);
    fog.nearDist = c.fogNear;
    fog.farDist = c.fogFar;
    fog.enabled = 1.0f;
    renderer->SetFog(fog);
  }

  if (postProcess) {
    postProcess->SetExposure(c.exposure);
    postProcess->SetUseBloom(c.useBloom);
    postProcess->SetBloomThreshold(c.bloomThreshold);
    postProcess->SetBloomIntensity(c.bloomIntensity);
    postProcess->SetBloomSigma(c.bloomSigma);
    postProcess->SetUseVignette(c.useVignette);
    postProcess->SetVignetteScale(c.vignetteScale);
    postProcess->SetVignetteExponent(c.vignetteExponent);
  }
}

void StageLighting::ResetSharedLight(Object3dRenderer *renderer) {
  if (!renderer) {
    return;
  }
  if (auto *dl = renderer->GetDirectionalLightData()) {
    dl->skyAmbient = {0.0f, 0.0f, 0.0f, 0.0f};
    dl->groundAmbient = {0.0f, 0.0f, 0.0f, 0.0f};
    dl->backRimColor = {0.0f, 0.0f, 0.0f, 0.0f};
  }
}

void StageLighting::LoadConfig() {
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
  for (const Section &section : kSections) {
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_array() && it->size() == 3) {
        config_.*item.member = {(*it)[0].get<float>(), (*it)[1].get<float>(),
                                (*it)[2].get<float>()};
      }
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_number()) {
        config_.*item.member = it->get<float>();
      }
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_boolean()) {
        config_.*item.member = it->get<bool>();
      }
    }
  }
}

void StageLighting::SaveConfig() const {
  nlohmann::json root;
  for (const Section &section : kSections) {
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      const Vector3 &v = config_.*item.member;
      root[item.key] = {v.x, v.y, v.z};
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      root[item.key] = config_.*item.member;
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      root[item.key] = config_.*item.member;
    }
  }
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void StageLighting::DrawImGui() {
#ifdef USE_IMGUI
  for (const Section &section : kSections) {
    if (!ImGui::TreeNode(section.title)) {
      continue;
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      ImGui::Checkbox(item.key, &(config_.*item.member));
    }
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      Vector3 &v = config_.*item.member;
      if (item.kind == Vec3Kind::Color) {
        // 1 を超える色（HDR）も入れられるようにする
        ImGui::ColorEdit3(item.key, &v.x, ImGuiColorEditFlags_HDR |
                                              ImGuiColorEditFlags_Float);
      } else {
        ImGui::DragFloat3(item.key, &v.x, 0.01f, -1.0f, 1.0f);
      }
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      ImGui::SliderFloat(item.key, &(config_.*item.member), item.min,
                         item.max);
    }
    ImGui::TreePop();
  }
  if (ImGui::Button("Save Lighting Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Lighting Config")) {
    LoadConfig();
  }
#endif // USE_IMGUI
}
