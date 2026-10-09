#include "FractureCache.h"
#include "../../externals/nlohmann/json.hpp"
#include "Effect/FracturePieceSet.h"
#include "Render/Model/Model.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 調整値の保存先
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/FractureConfig.json";

// 種の数の初期値と範囲（≒ 1体あたりの破片の数）
// 計算量は O(種の数^2 x 三角形数)。ロード時に1回だけなので、見た目優先で決める
const int kDefaultSeedCount = 8;
const int kMinSeedCount = 1;
const int kMaxSeedCount = 32;

// 種を置く乱数のシード（同じモデルは毎回同じ割れ方になる）
const uint32_t kRandomSeed = 1;
} // namespace

FractureCache *FractureCache::GetInstance() {
  static FractureCache instance;
  return &instance;
}

FractureCache::FractureCache() {
  seedCount_ = kDefaultSeedCount;
  LoadConfig();
}

FractureCache::~FractureCache() = default;

void FractureCache::Prebuild(const Model *model) {
  if (!model || sets_.contains(model)) {
    return;
  }

  auto pieceSet = std::make_unique<FracturePieceSet>();
  pieceSet->Create(model->GetModelData(), static_cast<uint32_t>(seedCount_),
                   kRandomSeed);
  sets_.emplace(model, std::move(pieceSet));
}

const FracturePieceSet *FractureCache::Find(const Model *model) const {
  auto it = sets_.find(model);
  if (it == sets_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void FractureCache::Clear() { sets_.clear(); }

void FractureCache::DrawEditorUI() {
#ifdef USE_IMGUI
  if (!ImGui::CollapsingHeader("Fracture (撃破時の破砕)")) {
    return;
  }

  FractureDebris::Params &p = debrisParams_;
  isConfigDirty_ |= ImGui::DragFloat("外向きの速さ", &p.explosionSpeed, 0.1f, 0.0f, 200.0f);
  isConfigDirty_ |= ImGui::DragFloat3("重力", &p.gravity.x, 0.1f);
  isConfigDirty_ |= ImGui::DragFloat("角速度の最大", &p.maxAngularSpeed, 0.1f, 0.0f, 60.0f);
  isConfigDirty_ |= ImGui::DragFloat("寿命（秒）", &p.lifetime, 0.01f, 0.05f, 10.0f);
  isConfigDirty_ |= ImGui::DragFloat("空気抵抗", &p.drag, 0.05f, 0.0f, 30.0f);

  isConfigDirty_ |= ImGui::SliderInt("種の数", &seedCount_, kMinSeedCount, kMaxSeedCount);
  ImGui::TextDisabled("種の数は保存して再起動すると反映されます");

  const char *saveLabel = isConfigDirty_ ? "[* 未保存] Save Fracture Config"
                                         : "Save Fracture Config";
  if (ImGui::Button(saveLabel)) {
    SaveConfig();
  }
#endif // USE_IMGUI
}

void FractureCache::LoadConfig() {
  std::ifstream file(kConfigPath);
  if (file.is_open()) {
    try {
      nlohmann::json root;
      file >> root;
      FractureDebris::Params &p = debrisParams_;
      if (root.contains("explosionSpeed")) {
        p.explosionSpeed = root["explosionSpeed"];
      }
      if (root.contains("gravity")) {
        p.gravity = {root["gravity"][0], root["gravity"][1], root["gravity"][2]};
      }
      if (root.contains("maxAngularSpeed")) {
        p.maxAngularSpeed = root["maxAngularSpeed"];
      }
      if (root.contains("lifetime")) {
        p.lifetime = root["lifetime"];
      }
      if (root.contains("drag")) {
        p.drag = root["drag"];
      }
      if (root.contains("seedCount")) {
        seedCount_ = root["seedCount"];
      }
    } catch (...) {
      // 読めなかった項目は初期値のまま使う
    }
  }
  isConfigDirty_ = false;
}

void FractureCache::SaveConfig() {
  const FractureDebris::Params &p = debrisParams_;
  nlohmann::json root;
  root["explosionSpeed"] = p.explosionSpeed;
  root["gravity"] = {p.gravity.x, p.gravity.y, p.gravity.z};
  root["maxAngularSpeed"] = p.maxAngularSpeed;
  root["lifetime"] = p.lifetime;
  root["drag"] = p.drag;
  root["seedCount"] = seedCount_;

  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
    isConfigDirty_ = false;
  }
}
