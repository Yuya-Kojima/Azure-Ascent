#include "StageRecord.h"
#include "../../externals/nlohmann/json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
const char *const kSaveDirectory = "save";
const char *const kSavePath = "save/records.json";

/// <summary>
/// 記録のファイル全体を読む（なければ空）
/// </summary>
nlohmann::json LoadAll() {
  std::ifstream file(kSavePath);
  if (!file.is_open()) {
    return nlohmann::json::object();
  }
  nlohmann::json root;
  try {
    file >> root;
  } catch (const nlohmann::json::exception &) {
    return nlohmann::json::object();
  }
  return root.is_object() ? root : nlohmann::json::object();
}
} // namespace

StageRecord StageRecord::Load(const std::string &stageName) {
  StageRecord record;
  const nlohmann::json root = LoadAll();
  if (!root.contains(stageName) || !root[stageName].is_object()) {
    return record;
  }
  const nlohmann::json &stage = root[stageName];
  try {
    record.hasRecord = true;
    record.bestScore = stage.value("bestScore", 0);
    record.bestRank = static_cast<Rank>(
        std::clamp(stage.value("bestRank", 0), static_cast<int>(Rank::C),
                   static_cast<int>(Rank::S)));
    record.bestKillRate = stage.value("bestKillRate", 0.0f);
  } catch (const nlohmann::json::exception &) {
    return StageRecord{};
  }
  return record;
}

void StageRecord::Save(const std::string &stageName,
                       const StageRecord &record) {
  nlohmann::json root = LoadAll();
  nlohmann::json stage;
  stage["bestScore"] = record.bestScore;
  stage["bestRank"] = static_cast<int>(record.bestRank);
  stage["bestKillRate"] = record.bestKillRate;
  root[stageName] = stage;
  if (!std::filesystem::exists(kSaveDirectory)) {
    std::filesystem::create_directories(kSaveDirectory);
  }
  std::ofstream file(kSavePath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}
