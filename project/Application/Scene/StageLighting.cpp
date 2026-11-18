#include "StageLighting.h"
#include "../../externals/nlohmann/json.hpp"
#include "Math/MathUtil.h"
#include "Render/CloudSea/CloudSea.h"
#include "Render/Renderer/Fog.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Render/Renderer/PostProcess.h"
#include "Render/SkyBox/SkyBox.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numbers>

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

// 晴れた空の値のキー（JSON の中で、この名前の物の中に同じキーで並べる）
const char *const kClearKey = "clear";
// 晴れの円の境目の値のキー
const char *const kClearEdgeKey = "clearEdge";

Vector4 ToVector4(const Vector3 &v, float w) { return {v.x, v.y, v.z, w}; }

// 度をラジアンにする
float ToRadians(float degrees) {
  return degrees * std::numbers::pi_v<float> / 180.0f;
}

/// <summary>
/// 晴れた空の初めの値（青空・高い白い太陽・薄い白い雲・明るい光）
/// </summary>
StageLighting::Config MakeDefaultClearConfig() {
  StageLighting::Config c;
  c.skyZenith = {0.10f, 0.28f, 0.75f};
  c.skyHorizon = {0.55f, 0.72f, 0.95f};
  c.sunDir = {0.1f, 0.6f, 1.0f};
  c.sunColor = {1.0f, 0.92f, 0.8f};
  c.sunIntensity = 1.0f;
  c.horizonGlow = 0.5f;
  c.cloudAmount = 0.35f;
  c.cloudColor = {0.85f, 0.88f, 0.95f};
  c.cloudOpacity = 0.8f;
  c.cloudRim = 0.3f;
  c.lightColor = {1.0f, 0.97f, 0.9f};
  c.lightIntensity = 1.2f;
  c.skyAmbient = {0.55f, 0.65f, 0.85f};
  c.groundAmbient = {0.35f, 0.38f, 0.42f};
  c.fogNear = 200.0f;
  c.fogFar = 1200.0f;
  c.exposure = 1.0f;
  return c;
}

/// <summary>
/// 2 組の値を割合 t で混ぜる（オンオフの値は t が半分を超えたら b）
/// </summary>
StageLighting::Config LerpConfig(const StageLighting::Config &a,
                                 const StageLighting::Config &b, float t) {
  StageLighting::Config result = a;
  for (const Section &section : kSections) {
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      result.*item.member = Lerp(a.*item.member, b.*item.member, t);
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      result.*item.member = Lerp(a.*item.member, b.*item.member, t);
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      result.*item.member = t > 0.5f ? b.*item.member : a.*item.member;
    }
  }
  return result;
}

/// <summary>
/// JSON の物から値を読む（ないキーは今の値のまま）
/// </summary>
void ReadConfig(const nlohmann::json &root, StageLighting::Config &config) {
  for (const Section &section : kSections) {
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_array() && it->size() == 3) {
        config.*item.member = {(*it)[0].get<float>(), (*it)[1].get<float>(),
                               (*it)[2].get<float>()};
      }
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_number()) {
        config.*item.member = it->get<float>();
      }
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      const auto it = root.find(item.key);
      if (it != root.end() && it->is_boolean()) {
        config.*item.member = it->get<bool>();
      }
    }
  }
}

/// <summary>
/// 値を JSON の物へ書く
/// </summary>
void WriteConfig(nlohmann::json &root, const StageLighting::Config &config) {
  for (const Section &section : kSections) {
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      const Vector3 &v = config.*item.member;
      root[item.key] = {v.x, v.y, v.z};
    }
    for (size_t i = 0; i < section.floatCount; ++i) {
      const FloatItem &item = section.floatItems[i];
      root[item.key] = config.*item.member;
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      root[item.key] = config.*item.member;
    }
  }
}

#ifdef USE_IMGUI
/// <summary>
/// 1 組の値の ImGui（見出しごとに畳む）
/// </summary>
void DrawConfigImGui(StageLighting::Config &config) {
  for (const Section &section : kSections) {
    if (!ImGui::TreeNode(section.title)) {
      continue;
    }
    for (size_t i = 0; i < section.boolCount; ++i) {
      const BoolItem &item = section.boolItems[i];
      ImGui::Checkbox(item.key, &(config.*item.member));
    }
    for (size_t i = 0; i < section.vec3Count; ++i) {
      const Vec3Item &item = section.vec3Items[i];
      Vector3 &v = config.*item.member;
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
      ImGui::SliderFloat(item.key, &(config.*item.member), item.min,
                         item.max);
    }
    ImGui::TreePop();
  }
}
#endif // USE_IMGUI
} // namespace

void StageLighting::Initialize() {
  clearConfig_ = MakeDefaultClearConfig();
  LoadConfig();
}

float StageLighting::GetClearAmount() const {
  return (std::max)(clearState_.amount, clearPreview_);
}

void StageLighting::FillCloudSeaClearing(CloudSeaClearing &out,
                                         float skyTime) const {
  // 円の外側の霧は空の淀んだ空の地平線の色、内側は晴れた空の地平線の色
  const Config dark = LerpConfig(config_, clearConfig_, clearPreview_);
  out.center = Normalize(clearState_.center);
  out.radius = clearState_.radius;
  out.softness = ToRadians(clearEdge_.softnessDeg);
  out.darkWidth = ToRadians(clearEdge_.darkWidthDeg);
  out.darkStrength = clearEdge_.darkStrength * clearState_.edgeDarkScale;
  out.noiseWidth = ToRadians(clearEdge_.noiseDeg);
  out.skyTime = skyTime;
  out.darkFogColor = dark.skyHorizon;
  out.clearFogColor = clearConfig_.skyHorizon;
}

void StageLighting::Apply(Skybox *skybox, Object3dRenderer *renderer,
                          PostProcess *postProcess, float skyTime) const {
  // 光・霧・ポストエフェクトは、晴れの割合で 2 組の値を混ぜる
  const Config c = LerpConfig(config_, clearConfig_, GetClearAmount());

  // 空：空の色・太陽・雲・地平線の光
  // 淀んだ空を晴れの円の外側に、晴れた空を内側に描く（淀んだ空は、確かめるための混ぜ具合だけ晴れへ寄せる）
  if (skybox) {
    const Config dark = LerpConfig(config_, clearConfig_, clearPreview_);
    const Config &clear = clearConfig_;
    skybox->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    skybox->SetSkyColors(dark.skyZenith, dark.skyHorizon);
    skybox->SetSun(Normalize(dark.sunDir), dark.sunColor, dark.sunIntensity,
                   dark.cloudAmount);
    skybox->SetCloudColor(dark.cloudColor, dark.cloudOpacity, dark.cloudRim);
    skybox->SetHorizonGlow(dark.horizonGlow, dark.horizonGlowHeight,
                           dark.horizonGlowAzimuth);
    skybox->SetClearSkyColors(clear.skyZenith, clear.skyHorizon);
    const Vector3 clearSunDir = clearState_.hasSunDirection
                                    ? clearState_.sunDirection
                                    : clear.sunDir;
    skybox->SetClearSun(Normalize(clearSunDir), clear.sunColor,
                        clear.sunIntensity * clearState_.sunIntensityScale,
                        clear.cloudAmount);
    skybox->SetClearCloudColor(clear.cloudColor, clear.cloudOpacity,
                               clear.cloudRim);
    skybox->SetClearHorizonGlow(clear.horizonGlow, clear.horizonGlowHeight,
                                clear.horizonGlowAzimuth);
    skybox->SetClearCircle(Normalize(clearState_.center), clearState_.radius);
    skybox->SetClearEdge(ToRadians(clearEdge_.softnessDeg),
                         ToRadians(clearEdge_.darkWidthDeg),
                         clearEdge_.darkStrength * clearState_.edgeDarkScale,
                         ToRadians(clearEdge_.noiseDeg));
    skybox->SetSkyTime(skyTime);
  }

  if (renderer) {
    // 平行光源：全シーン共通なので、このシーンの値で毎フレーム上書きする
    if (auto *dl = renderer->GetDirectionalLightData()) {
      dl->color = ToVector4(c.lightColor, 1.0f);
      // シェーダーは「光が進む向き」を使うので、光が来る方向の逆にする
      // 晴れた空に太陽が見えているときは、光が来る方向を晴れの割合だけ太陽へ寄せる
      Vector3 lightFrom = Normalize(c.lightFrom);
      Vector3 backRimFrom = Normalize(c.backRimFrom);
      if (clearState_.hasSunDirection) {
        const Vector3 sun = Normalize(clearState_.sunDirection);
        lightFrom = Normalize(Lerp(lightFrom, sun, clearState_.amount));
        backRimFrom = Normalize(Lerp(backRimFrom, sun, clearState_.amount));
      }
      dl->direction = lightFrom * -1.0f;
      dl->intensity = c.lightIntensity;
      dl->skyAmbient = ToVector4(c.skyAmbient, 0.0f);
      dl->groundAmbient = ToVector4(c.groundAmbient, 0.0f);
      dl->backRimColor = ToVector4(c.backRimColor, c.backRimIntensity);
      dl->backRimPower = c.backRimPower;
      dl->backRimDirection = backRimFrom * -1.0f;
    }

    // 霧：遠くを地平線の色に溶かす
    // 晴れの円があるときは、空と同じ円の外側を淀んだ空、内側を晴れた空の地平線の色にする
    FogData fog;
    fog.color = ToVector4(c.skyHorizon, 1.0f);
    fog.nearDist = c.fogNear;
    fog.farDist = c.fogFar;
    fog.enabled = 1.0f;
    if (clearState_.radius > 0.0f) {
      const Config dark = LerpConfig(config_, clearConfig_, clearPreview_);
      fog.color = ToVector4(dark.skyHorizon, 1.0f);
      fog.clearColor = ToVector4(clearConfig_.skyHorizon, 1.0f);
      const Vector3 center = Normalize(clearState_.center);
      fog.clearCircle = {center.x, center.y, center.z, clearState_.radius};
      fog.clearEdge = {ToRadians(clearEdge_.softnessDeg),
                       ToRadians(clearEdge_.darkWidthDeg),
                       clearEdge_.darkStrength * clearState_.edgeDarkScale,
                       ToRadians(clearEdge_.noiseDeg)};
      fog.skyTime = skyTime;
    }
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
  ReadConfig(root, config_);
  const auto clearIt = root.find(kClearKey);
  if (clearIt != root.end() && clearIt->is_object()) {
    ReadConfig(*clearIt, clearConfig_);
  }
  const auto edgeIt = root.find(kClearEdgeKey);
  if (edgeIt != root.end() && edgeIt->is_object()) {
    const nlohmann::json &edge = *edgeIt;
    clearEdge_.softnessDeg = edge.value("softnessDeg", clearEdge_.softnessDeg);
    clearEdge_.darkWidthDeg =
        edge.value("darkWidthDeg", clearEdge_.darkWidthDeg);
    clearEdge_.darkStrength =
        edge.value("darkStrength", clearEdge_.darkStrength);
    clearEdge_.noiseDeg = edge.value("noiseDeg", clearEdge_.noiseDeg);
  }
}

void StageLighting::SaveConfig() const {
  nlohmann::json root;
  WriteConfig(root, config_);
  nlohmann::json clear;
  WriteConfig(clear, clearConfig_);
  root[kClearKey] = clear;
  root[kClearEdgeKey] = {{"softnessDeg", clearEdge_.softnessDeg},
                         {"darkWidthDeg", clearEdge_.darkWidthDeg},
                         {"darkStrength", clearEdge_.darkStrength},
                         {"noiseDeg", clearEdge_.noiseDeg}};
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
  DrawConfigImGui(config_);
  // 晴れた空の値（クリア演出で、晴れの円の内側と、晴れたあとの光・霧・ポストエフェクトに使う）
  if (ImGui::TreeNode("Clear Sky")) {
    ImGui::PushID(kClearKey);
    // 晴れた空を全体に混ぜて見る（保存しない）
    ImGui::SliderFloat("Preview", &clearPreview_, 0.0f, 1.0f);
    DrawConfigImGui(clearConfig_);
    ImGui::SeparatorText("Clear Edge");
    ImGui::SliderFloat("Softness Deg", &clearEdge_.softnessDeg, 0.1f, 60.0f);
    ImGui::SliderFloat("Dark Width Deg", &clearEdge_.darkWidthDeg, 0.1f, 90.0f);
    ImGui::SliderFloat("Dark Strength", &clearEdge_.darkStrength, 0.0f, 1.0f);
    ImGui::SliderFloat("Noise Deg", &clearEdge_.noiseDeg, 0.0f, 30.0f);
    ImGui::PopID();
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
