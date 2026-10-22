#pragma once
#include "Core/Dx12Core.h"
#include "Core/SrvHandle.h"
#include "DirectXTex.h"
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

class SrvManager;

class TextureManager {

public:
  /// <summary>
  /// シングルトンインスタンスの取得
  /// </summary>
  /// <returns></returns>
  static TextureManager *GetInstance();

  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(Dx12Core *dx12Core, SrvManager *srvManager);

  /// <summary>
  /// 終了
  /// </summary>
  static void Finalize();

  /// <summary>
  /// テクスチャファイルの読み込み
  /// </summary>
  /// <param name="filePath">テクスチャファイルパス</param>
  /// <returns>画像イメージデータ</returns>
  void LoadTexture(const std::string &filePath);

  /// <summary>
  /// メモリ上のピクセルデータからテクスチャを生成・登録する
  /// </summary>
  /// <param name="name">登録名（キー名）</param>
  /// <param name="pixels">ピクセルデータ（RGBA）</param>
  /// <param name="width">幅</param>
  /// <param name="height">高さ</param>
  void LoadTextureFromMemory(const std::string &name, const uint8_t *pixels, int width, int height);

private:
  TextureManager() = default;
  ~TextureManager() = default;
  TextureManager(TextureManager &) = delete;
  TextureManager &operator=(TextureManager &) = delete;

  // テクスチャ一枚分のデータ
  struct TextureData {
    DirectX::TexMetadata metadata;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    SrvHandle srv;
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
  };

  // テクスチャデータ
  // std::vector<TextureData> textureDatas;

  Dx12Core *dx12Core_ = nullptr;

  // SRVインデックスの開始番号
  // static uint32_t kSRVIndexTop;

public:
  /// <summary>
  /// SRVインデックスの開始番号
  /// </summary>
  /// <param name="filePath"></param>
  /// <returns></returns>
  // uint32_t GetTextureIndexByFilePath(const std::string &filePath);

  /// <summary>
  /// ファイルパスからGPUハンドルを取得
  /// </summary>
  /// <param name="filePath"></param>
  /// <returns></returns>
  D3D12_GPU_DESCRIPTOR_HANDLE
  GetSrvHandleGPU(const std::string &filePath) const;

  /// <summary>
  /// メタデータの取得
  /// </summary>
  /// <param name="filePath"></param>
  /// <returns></returns>
  const DirectX::TexMetadata &GetMetaData(const std::string &filePath) const;

  /// <summary>
  /// SRVインデックスの取得
  /// </summary>
  /// <param name="filePath"></param>
  /// <returns></returns>
  uint32_t GetSrvIndex(const std::string &filePath) const;

private:
  SrvManager *srvManager_ = nullptr;

  std::unordered_map<std::string, TextureData> textureDatas;

  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;
};
