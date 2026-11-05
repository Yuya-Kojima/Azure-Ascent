#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>

class Model;

class ModelRenderer;

class Dx12Core;

class ModelManager {

public:
  static ModelManager *GetInstance();

  static void Finalize();

  void Initialize(Dx12Core *dx12Core);

private:
  static bool isFinalized_;

  ModelManager() = default;
  ~ModelManager() = default;
  ModelManager(ModelManager &) = delete;
  ModelManager &operator=(ModelManager) = delete;

private:
  // モデルデータ
  std::map<std::string, std::unique_ptr<Model>> models;

  // 読み込みに失敗したモデルのパス（同じパスを読み直さず、ログも 1 回だけにする）
  std::set<std::string> failedModels_;

  std::unique_ptr<ModelRenderer> modelRenderer = nullptr;

  void CreateBuiltinPrimitives();

public:
  /// <summary>
  /// モデルファイルの読み込み
  /// </summary>
  /// <param name="filePath">ファイルパス</param>
  void LoadModel(const std::string &filePath);

  /// <summary>
  /// モデルの検索
  /// </summary>
  /// <param name="filePath"></param>
  Model *FindModel(const std::string &filePath);

  ModelRenderer *GetModelRenderer() const { return modelRenderer.get(); }

  static std::string ResolveModelPath(const std::string &input);
};
