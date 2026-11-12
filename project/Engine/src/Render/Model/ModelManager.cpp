#include "Model/ModelManager.h"
#include "Model/Model.h"
#include "Render/Mesh/MeshGenerator.h"
#include "Renderer/ModelRenderer.h"
#include "Debug/Logger.h"
#include <cassert>
#include <filesystem>

bool ModelManager::isFinalized_ = false;

namespace fs = std::filesystem;

std::string ModelManager::ResolveModelPath(const std::string &input) {
  const fs::path base = "resources";

  // すでにパスなら、そのまま見つかればそれを返し、
  // 見つからなければ resources 以下として見つかるか試す
  if (input.find('/') != std::string::npos ||
      input.find('\\') != std::string::npos) {
    if (!fs::exists(input) && fs::exists(base / input)) {
      return (base / input).generic_string();
    }
    return input;
  }

  // resources 以下を再帰検索
  if (fs::exists(base)) {
    for (const auto &e : fs::recursive_directory_iterator(base)) {
      if (!e.is_regular_file()) {
        continue;
      }

      if (e.path().filename() == input) {
        return e.path().generic_string();
      }
    }
  }

  // 見つからなければ、そのまま
  return input;
}

ModelManager *ModelManager::GetInstance() {

  // すでにFinalize済ならエラーを吐く
  if (isFinalized_) {
    assert(!"ModelManager::GetInstance() was called after Finalize()!");
    return nullptr;
  }

  static ModelManager instance;
  return &instance;
}

void ModelManager::Finalize() {
  // すでにFinalize済なら警告
  if (isFinalized_) {
    assert(!"ModelManager::Finalize() called twice!");
    return;
  }

  ModelManager *instance = GetInstance();
  instance->modelRenderer.reset();
  instance->models.clear();

  isFinalized_ = true;
}

void ModelManager::Initialize(Dx12Core *dx12Core) {

  modelRenderer = std::make_unique<ModelRenderer>();
  modelRenderer->Initialize(dx12Core);

  CreateBuiltinPrimitives();
}

void ModelManager::CreateBuiltinPrimitives() {
  auto registerPrimitive = [this](const std::string &name, const Model::ModelData &data) {
    auto model = std::make_unique<Model>();
    model->InitializeFromModelData(modelRenderer.get(), data);
    models[name] = std::move(model);
  };

  registerPrimitive("__builtin_cylinder", RC::MeshGenerator::GenerateCylinder(0.5f, 2.0f, 16));
  registerPrimitive("__builtin_box", RC::MeshGenerator::GenerateBox(1.0f, 1.0f, 1.0f));
  registerPrimitive("__builtin_sphere", RC::MeshGenerator::GenerateSphere(1.0f, 16, 16));
  registerPrimitive("__builtin_capsule", RC::MeshGenerator::GenerateCapsule(0.5f, 2.0f, 16, 16));
  registerPrimitive("__builtin_cone", RC::MeshGenerator::GenerateCone(0.5f, 1.0f, 16));
  registerPrimitive("__builtin_plane", RC::MeshGenerator::GeneratePlane(1.0f, 1.0f, 1, 1));
  registerPrimitive("__builtin_crystal", RC::MeshGenerator::GenerateCrystal(0.5f, 2.0f, 6));
}

void ModelManager::LoadModel(const std::string &filePath) {

  const std::string resolved = ResolveModelPath(filePath);

  if (models.contains(resolved) || failedModels_.contains(resolved)) {
    // 読み込み済み、または前に失敗したパスなら早期リターン
    return;
  }

  // "resources/" を基準にする
  const std::string baseDir = "resources";

  // ディレクトリ部分とファイル名部分に分解
  std::string directoryPath = baseDir;
  std::string filename = resolved;

  const size_t slash = resolved.find_last_of("/\\");
  if (slash != std::string::npos) {
    directoryPath = resolved.substr(0, slash);
    filename = resolved.substr(slash + 1);
  }
  // if (slash != std::string::npos) {
  //   directoryPath = baseDir + "/" + filePath.substr(0, slash);
  //   filename = filePath.substr(slash + 1);
  // }

  std::unique_ptr<Model> model = std::make_unique<Model>();
  try {
      model->Initialize(modelRenderer.get(), directoryPath, filename);
      models.insert(std::make_pair(resolved, std::move(model)));
  } catch (const std::exception& e) {
      // 失敗した場合はエラーログを出力し、マップには登録せず、失敗したパスとして覚える
      Logger::Log(std::string("[ModelManager] Failed to load model: ") + resolved + " - " + e.what() + "\n");
      failedModels_.insert(resolved);
  }

  //// モデルの生成とファイル読み込み、初期化
  // std::unique_ptr<Model> model = std::make_unique<Model>();
  // model->Initialize(modelRenderer, "resources", filePath);

  //// モデルをmapコンテナに格納
  // models.insert(std::make_pair(filePath, std::move(model)));
}

Model *ModelManager::FindModel(const std::string &filePath) {

  const std::string resolved = ResolveModelPath(filePath);

  // 読み込み済みモデルを検索
  if (models.contains(resolved)) {
    // 読み込みモデルを戻り値としてリターン
    return models.at(resolved).get();
  }

  // ファイル名一致なし
  return nullptr;
}
