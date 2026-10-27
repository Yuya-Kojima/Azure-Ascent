#include "StageSelectReturnSky.h"
#include "StageSelectLightning.h"
#include "../../externals/nlohmann/json.hpp"
#include "Render/CloudSea/CloudSea.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/Fog.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Render/Renderer/PostProcess.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/GameOverReturnSkyConfig.json";
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
// Object3d の鏡面反射の倍率の初期値（いつもの拠点の地面の値）
const float kDefaultSpecularScale = 1.0f;

Vector4 ToVector4(const Vector3 &v, float w) { return {v.x, v.y, v.z, w}; }

Vector4 Lerp4(const Vector4 &a, const Vector4 &b, float t) {
  return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t),
          Lerp(a.w, b.w, t)};
}
} // namespace

StageSelectReturnSky::StageSelectReturnSky()
    : lightning_(std::make_unique<StageSelectLightning>()) {}

StageSelectReturnSky::~StageSelectReturnSky() = default;

template <class Visitor>
void StageSelectReturnSky::VisitConfigValues(Visitor &&visit) {
  auto visitVector3 = [&](const std::string &key, Vector3 &v) {
    visit(key + ".x", v.x);
    visit(key + ".y", v.y);
    visit(key + ".z", v.z);
  };
  Lighting &l = lighting_;
  visitVector3("light.lightFrom", l.lightFrom);
  visitVector3("light.lightColor", l.lightColor);
  visit("light.lightIntensity", l.lightIntensity);
  visitVector3("light.skyAmbient", l.skyAmbient);
  visitVector3("light.groundAmbient", l.groundAmbient);
  visitVector3("light.rimFrom", l.rimFrom);
  visitVector3("light.rimColor", l.rimColor);
  visit("light.rimIntensity", l.rimIntensity);
  visit("light.rimPower", l.rimPower);
  visit("light.dragonRimScale", l.dragonRimScale);
  visit("light.groundSpecular", l.groundSpecular);
  visit("light.fogNear", l.fogNear);
  visit("light.fogFar", l.fogFar);
  visit("light.exposure", l.exposure);

  Ceiling &c = ceiling_;
  visit("ceiling.height", c.height);
  visit("ceiling.thickness", c.thickness);
  visit("ceiling.amplitude", c.amplitude);
  visit("ceiling.blobSize", c.blobSize);
  visit("ceiling.edgeSoftness", c.edgeSoftness);
  visit("ceiling.density", c.density);
  visitVector3("ceiling.color", c.color);
  visit("ceiling.brightness", c.brightness);
  visit("ceiling.powder", c.powder);
  visit("ceiling.valleyDarkness", c.valleyDarkness);
  visit("ceiling.noiseSize", c.noiseSize);
  visit("ceiling.detailSize", c.detailSize);
  visit("ceiling.detailStrength", c.detailStrength);
  visit("ceiling.wind.x", c.wind.x);
  visit("ceiling.wind.y", c.wind.y);

  lightning_->VisitConfigValues([&](const std::string &key, float &value) {
    visit("lightning." + key, value);
  });
}

void StageSelectReturnSky::LoadConfig() {
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
  lightning_->OnConfigLoaded();
}

void StageSelectReturnSky::SaveConfig() {
  nlohmann::json root;
  VisitConfigValues(
      [&](const std::string &key, float &value) { root[key] = value; });
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void StageSelectReturnSky::Initialize(CloudSeaRenderer *cloudSeaRenderer) {
  cloudSea_ = std::make_unique<CloudSea>();
  cloudSea_->Initialize(cloudSeaRenderer);
  lightning_->Initialize();
  LoadConfig();
}

void StageSelectReturnSky::Update(const ICamera *camera, float deltaTime,
                                  float lightningTime,
                                  const Vector3 &dragonPos, float dragonYaw) {
  time_ += deltaTime;
  camera_ = camera;

  const Ceiling &c = ceiling_;
  cloudSea_->SetHeight(c.height);
  cloudSea_->SetRelief(c.amplitude, c.blobSize);
  cloudSea_->SetWind(c.wind);
  CloudSeaLook look;
  look.thickness = c.thickness;
  look.edgeSoftness = c.edgeSoftness;
  look.density = c.density;
  look.color = c.color;
  look.brightness = c.brightness;
  look.powder = c.powder;
  look.valleyDarkness = c.valleyDarkness;
  look.noiseSize = c.noiseSize;
  look.detailSize = c.detailSize;
  look.detailStrength = c.detailStrength;
  // 地平線の光（逆光の縁）は雲には使わない。縁の光はドラゴンだけに当てる
  look.rimStrength = 0.0f;
  cloudSea_->SetLook(look);
  cloudSea_->ClearShapes();
  lightning_->Update(lightningTime, dragonPos, dragonYaw,
                     c.height - c.thickness);
  lightning_->ApplyGlows(cloudSea_.get());
  cloudSea_->SetTime(time_);
  cloudSea_->SetCamera(camera);
  cloudSea_->Update();
}

void StageSelectReturnSky::ApplyLighting(Object3dRenderer *renderer,
                                         FogData fog,
                                         PostProcess *postProcess,
                                         float baseExposure, float dragonYaw,
                                         float weight) const {
  //===========================
  // ドラゴンの水平な右・上・前で表した向きを、ワールドの向きにする
  //===========================
  const Vector3 forward = {std::sin(dragonYaw), 0.0f, std::cos(dragonYaw)};
  const Vector3 right = Normalize(Cross(kWorldUp, forward));
  auto toWorld = [&](const Vector3 &local) {
    return Normalize(right * local.x + kWorldUp * local.y + forward * local.z);
  };
  const Lighting &l = lighting_;

  if (auto *dl = renderer->GetDirectionalLightData()) {
    // シェーダーは「光が進む向き」を使うので、光が来る向きの逆にする
    const Vector3 returnDirection = toWorld(l.lightFrom) * -1.0f;
    dl->direction = Normalize(Lerp(dl->direction, returnDirection, weight));
    dl->color = Lerp4(dl->color, ToVector4(l.lightColor, 1.0f), weight);
    dl->intensity = Lerp(dl->intensity, l.lightIntensity, weight);
    dl->skyAmbient = Lerp4(dl->skyAmbient, ToVector4(l.skyAmbient, 0.0f), weight);
    dl->groundAmbient =
        Lerp4(dl->groundAmbient, ToVector4(l.groundAmbient, 0.0f), weight);
    dl->backRimColor = Lerp4(dl->backRimColor,
                            ToVector4(l.rimColor, l.rimIntensity), weight);
    dl->backRimPower = l.rimPower;
    dl->backRimDirection = toWorld(l.rimFrom) * -1.0f;
  }
  lightning_->ApplyLight(renderer, weight);

  fog.nearDist = Lerp(fog.nearDist, l.fogNear, weight);
  fog.farDist = Lerp(fog.farDist, l.fogFar, weight);
  renderer->SetFog(fog);

  if (postProcess) {
    postProcess->SetExposure(Lerp(baseExposure, l.exposure, weight));
    lightning_->ApplyPostProcess(postProcess);
  }
}

void StageSelectReturnSky::ApplyMaterials(Object3d *ground, Object3d *dragon,
                                          float weight) const {
  ground->SetSpecularScale(
      Lerp(kDefaultSpecularScale, lighting_.groundSpecular, weight));
  dragon->SetBackRimScale(lighting_.dragonRimScale * weight);
}

void StageSelectReturnSky::Draw() {
  cloudSea_->Draw();
  lightning_->Draw(camera_);
}

void StageSelectReturnSky::DrawImGui() {
#ifdef USE_IMGUI
  if (ImGui::Button("Save Return Sky Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Return Sky Config")) {
    LoadConfig();
  }

  // 1 を超える色（HDR）も入れられるようにする
  const ImGuiColorEditFlags kColorFlags =
      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
  Lighting &l = lighting_;
  ImGui::SeparatorText("Light (right, up, forward of dragon)");
  ImGui::DragFloat3("Light From", &l.lightFrom.x, 0.01f, -1.0f, 1.0f);
  ImGui::ColorEdit3("Light Color", &l.lightColor.x, kColorFlags);
  ImGui::DragFloat("Light Intensity", &l.lightIntensity, 0.01f, 0.0f, 3.0f);
  ImGui::ColorEdit3("Sky Ambient", &l.skyAmbient.x, kColorFlags);
  ImGui::ColorEdit3("Ground Ambient", &l.groundAmbient.x, kColorFlags);
  ImGui::DragFloat3("Rim From", &l.rimFrom.x, 0.01f, -1.0f, 1.0f);
  ImGui::ColorEdit3("Rim Color", &l.rimColor.x, kColorFlags);
  ImGui::DragFloat("Rim Intensity", &l.rimIntensity, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Rim Power", &l.rimPower, 0.01f, 0.5f, 8.0f);
  ImGui::DragFloat("Dragon Rim Scale", &l.dragonRimScale, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Ground Specular", &l.groundSpecular, 0.01f, 0.0f, 1.0f);
  ImGui::DragFloat("Fog Near", &l.fogNear, 1.0f, 0.0f, 1000.0f);
  ImGui::DragFloat("Fog Far", &l.fogFar, 1.0f, 1.0f, 3000.0f);
  ImGui::DragFloat("Exposure", &l.exposure, 0.01f, 0.05f, 3.0f);

  Ceiling &c = ceiling_;
  ImGui::SeparatorText("Cloud Ceiling");
  ImGui::DragFloat("Height", &c.height, 0.1f, -50.0f, 300.0f);
  ImGui::DragFloat("Thickness", &c.thickness, 0.1f, 1.0f, 100.0f);
  ImGui::DragFloat("Amplitude", &c.amplitude, 0.05f, 0.0f, 30.0f);
  ImGui::DragFloat("Blob Size", &c.blobSize, 0.1f, 1.0f, 200.0f);
  ImGui::DragFloat("Edge Softness", &c.edgeSoftness, 0.01f, 0.01f, 10.0f);
  ImGui::DragFloat("Density", &c.density, 0.01f, 0.0f, 10.0f);
  ImGui::ColorEdit3("Color", &c.color.x, kColorFlags);
  ImGui::DragFloat("Brightness", &c.brightness, 0.01f, 0.0f, 20.0f);
  ImGui::SliderFloat("Powder", &c.powder, 0.0f, 1.0f);
  ImGui::SliderFloat("Valley Darkness", &c.valleyDarkness, 0.0f, 1.0f);
  ImGui::DragFloat("Noise Size", &c.noiseSize, 0.1f, 1.0f, 300.0f);
  ImGui::DragFloat("Detail Size", &c.detailSize, 0.05f, 0.5f, 50.0f);
  ImGui::SliderFloat("Detail Strength", &c.detailStrength, 0.0f, 0.9f);
  ImGui::DragFloat2("Wind", &c.wind.x, 0.01f, -20.0f, 20.0f);

  lightning_->DrawImGui();
#endif
}
