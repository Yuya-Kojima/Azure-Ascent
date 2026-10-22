#include "Texture/TextureManager.h"
// #include "Dx12Core.h"
#include "Core/SrvManager.h"
#include "Debug/Logger.h"
#include "Util/StringUtil.h"
#include <filesystem>
#include <unordered_set>

// ImGuiで０番を使用するため、1番から使用する
// uint32_t TextureManager::kSRVIndexTop = 1;

TextureManager *TextureManager::GetInstance() {
  static TextureManager instance;
  return &instance;
}

void TextureManager::Initialize(Dx12Core *dx12Core, SrvManager *srvManager) {

  dx12Core_ = dx12Core;

  srvManager_ = srvManager;

  // SRVの数と同数
  // textureDatas.reserve(Dx12Core::kMaxSRVCount);
}

void TextureManager::Finalize() {
  TextureManager *instance = GetInstance();
  instance->textureDatas.clear();
  instance->intermediateResources_.clear();
  instance->dx12Core_ = nullptr;
  instance->srvManager_ = nullptr;
}

void TextureManager::LoadTexture(const std::string &filePath) {

  if (textureDatas.contains(filePath)) {
    return;
  }

  DirectX::ScratchImage image{};
  DirectX::ScratchImage mipImages{};
  std::wstring filePathW = StringUtil::ConvertString(filePath);

  HRESULT hr = S_OK;

  //========================
  // 読み込み
  //========================
  if (filePathW.size() >= 4 &&
      filePathW.substr(filePathW.size() - 4) == L".dds") {
    hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE,
                                  nullptr, image);
  } else {
    hr = DirectX::LoadFromWICFile(
        filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
  }
  
  // 読み込み失敗時のフォールバック処理
  if (FAILED(hr)) {
    Logger::Log("Texture failed to load: " + filePath + ", fallback to white1x1.png\n");
    hr = DirectX::LoadFromWICFile(L"resources/white1x1.png", DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    if (FAILED(hr)) {
      Logger::Log("Fallback texture white1x1.png also failed to load.\n");
      return;
    }
  }

  const auto &meta = image.GetMetadata();

  //========================
  // MipMap
  //========================
  if (DirectX::IsCompressed(meta.format)) {
    mipImages = std::move(image);
  } else {
    bool needMip = (meta.mipLevels <= 1) && (meta.width > 1 || meta.height > 1);

    if (needMip) {
      hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(),
                                    meta, DirectX::TEX_FILTER_DEFAULT, 0,
                                    mipImages);
      assert(SUCCEEDED(hr));
    } else {
      mipImages = std::move(image);
    }
  }

  TextureData &textureData = textureDatas[filePath];

  textureData.metadata = mipImages.GetMetadata();
  textureData.resource = dx12Core_->CreateTextureResource(textureData.metadata);

  intermediateResources_.push_back(
      dx12Core_->UploadTextureData(textureData.resource, mipImages));

  textureData.srv = srvManager_->Allocate();
  textureData.srvHandleCPU =
      srvManager_->GetCPUDescriptorHandle(textureData.srv.Index());
  textureData.srvHandleGPU =
      srvManager_->GetGPUDescriptorHandle(textureData.srv.Index());

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = textureData.metadata.format;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  if (textureData.metadata.IsCubemap()) {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = textureData.metadata.mipLevels
                                        ? UINT(textureData.metadata.mipLevels)
                                        : 1u;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
  } else {
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = textureData.metadata.mipLevels
                                      ? UINT(textureData.metadata.mipLevels)
                                      : 1u;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
  }

  dx12Core_->GetDevice()->CreateShaderResourceView(
      textureData.resource.Get(), &srvDesc, textureData.srvHandleCPU);
}

void TextureManager::LoadTextureFromMemory(const std::string &name, const uint8_t *pixels, int width, int height) {
  if (textureDatas.contains(name)) {
    return;
  }

  DirectX::ScratchImage image{};
  HRESULT hr = image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 1);
  assert(SUCCEEDED(hr));

  uint8_t* dest = image.GetPixels();
  size_t rowPitch = image.GetImages()->rowPitch;
  size_t srcRowPitch = width * 4; // RGBA 8bit

  for (int y = 0; y < height; ++y) {
    memcpy(dest + y * rowPitch, pixels + y * srcRowPitch, srcRowPitch);
  }

  const auto &meta = image.GetMetadata();

  TextureData &textureData = textureDatas[name];
  textureData.metadata = meta;
  textureData.resource = dx12Core_->CreateTextureResource(textureData.metadata);

  intermediateResources_.push_back(
      dx12Core_->UploadTextureData(textureData.resource, image));

  textureData.srv = srvManager_->Allocate();
  textureData.srvHandleCPU = srvManager_->GetCPUDescriptorHandle(textureData.srv.Index());
  textureData.srvHandleGPU = srvManager_->GetGPUDescriptorHandle(textureData.srv.Index());

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.Format = textureData.metadata.format;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MostDetailedMip = 0;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.PlaneSlice = 0;
  srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

  dx12Core_->GetDevice()->CreateShaderResourceView(
      textureData.resource.Get(), &srvDesc, textureData.srvHandleCPU);
}

D3D12_GPU_DESCRIPTOR_HANDLE
TextureManager::GetSrvHandleGPU(const std::string &filePath) const {

  if (!textureDatas.contains(filePath)) {
    static std::unordered_set<std::string> loggedErrors;
    if (!loggedErrors.contains(filePath)) {
      Logger::Log("Texture not found in GetSrvHandleGPU: " + filePath + "\n");
      loggedErrors.insert(filePath);
    }
    if (!textureDatas.empty()) {
      return textureDatas.begin()->second.srvHandleGPU;
    }
    return D3D12_GPU_DESCRIPTOR_HANDLE{};
  }

  const TextureData &textureData = textureDatas.at(filePath);
  return textureData.srvHandleGPU;
}

const DirectX::TexMetadata &
TextureManager::GetMetaData(const std::string &filePath) const {
  if (!textureDatas.contains(filePath)) {
    if (!textureDatas.empty()) {
      return textureDatas.begin()->second.metadata;
    }
    static DirectX::TexMetadata emptyMeta{};
    return emptyMeta;
  }
  const TextureData &textureData = textureDatas.at(filePath);
  return textureData.metadata;
}

uint32_t TextureManager::GetSrvIndex(const std::string &filePath) const {

  assert(textureDatas.contains(filePath));

  const TextureData &textureData = textureDatas.at(filePath);
  return textureData.srv.Index();
}
