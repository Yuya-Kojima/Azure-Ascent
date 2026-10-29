#include "ScoreConfig.h"
#include "../../externals/nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/ScoreConfig.json";
// 基準の並び（S, A, B）に対応するランク
const std::array<Rank, ScoreConfig::kRankBorderCount> kBorderRanks = {
    Rank::S, Rank::A, Rank::B};

/// <summary>
/// rank の基準の並びの中の位置（C なら -1）
/// </summary>
int BorderIndex(Rank rank) {
  for (int i = 0; i < ScoreConfig::kRankBorderCount; ++i) {
    if (kBorderRanks[i] == rank) {
      return i;
    }
  }
  return -1;
}
} // namespace

void ScoreConfig::Load() {
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
  auto read = [&root](const char *key, auto &value) {
    if (root.contains(key) && root[key].is_number()) {
      value = root[key].get<std::remove_reference_t<decltype(value)>>();
    }
  };
  read("pointPerKill", pointPerKill);
  read("maxLockMultiplier", maxLockMultiplier);
  read("bossPoint", bossPoint);
  if (root.contains("scoreBorders") && root["scoreBorders"].is_array()) {
    for (int i = 0; i < kRankBorderCount && i < static_cast<int>(root["scoreBorders"].size()); ++i) {
      if (root["scoreBorders"][i].is_number()) {
        scoreBorders[i] = root["scoreBorders"][i].get<int>();
      }
    }
  }
  if (root.contains("killRateBorders") && root["killRateBorders"].is_array()) {
    for (int i = 0; i < kRankBorderCount && i < static_cast<int>(root["killRateBorders"].size()); ++i) {
      if (root["killRateBorders"][i].is_number()) {
        killRateBorders[i] = root["killRateBorders"][i].get<float>();
      }
    }
  }
}

void ScoreConfig::Save() const {
  nlohmann::json root;
  root["pointPerKill"] = pointPerKill;
  root["maxLockMultiplier"] = maxLockMultiplier;
  root["bossPoint"] = bossPoint;
  root["scoreBorders"] = scoreBorders;
  root["killRateBorders"] = killRateBorders;
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void ScoreConfig::DrawImGui() {
#ifdef USE_IMGUI
  if (ImGui::Button("Save Score Config")) {
    Save();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Score Config")) {
    Load();
  }
  ImGui::DragInt("Point Per Kill", &pointPerKill, 1.0f, 0, 10000);
  ImGui::DragFloat("Max Lock Multiplier", &maxLockMultiplier, 0.05f, 1.0f, 10.0f);
  ImGui::DragInt("Boss Point", &bossPoint, 100.0f, 0, 1000000);
  ImGui::DragInt3("Score Borders (S A B)", scoreBorders.data(), 100.0f, 0, 10000000);
  ImGui::SliderFloat3("Kill Rate Borders (S A B)", killRateBorders.data(), 0.0f, 1.0f);
#endif
}

Rank ScoreConfig::GetScoreRank(int score) const {
  for (int i = 0; i < kRankBorderCount; ++i) {
    if (score >= scoreBorders[i]) {
      return kBorderRanks[i];
    }
  }
  return Rank::C;
}

Rank ScoreConfig::GetKillRateRank(float killRate) const {
  for (int i = 0; i < kRankBorderCount; ++i) {
    if (killRate >= killRateBorders[i]) {
      return kBorderRanks[i];
    }
  }
  return Rank::C;
}

Rank ScoreConfig::GetOverallRank(Rank scoreRank, Rank killRateRank) {
  return (std::min)(scoreRank, killRateRank);
}

int ScoreConfig::GetScoreBorder(Rank rank) const {
  const int index = BorderIndex(rank);
  return index < 0 ? 0 : scoreBorders[index];
}

float ScoreConfig::GetKillRateBorder(Rank rank) const {
  const int index = BorderIndex(rank);
  return index < 0 ? 0.0f : killRateBorders[index];
}
