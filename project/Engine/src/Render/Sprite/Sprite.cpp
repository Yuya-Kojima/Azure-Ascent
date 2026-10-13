#include "Sprite/Sprite.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Renderer/SpriteRenderer.h"
#include "Texture/TextureManager.h"

void Sprite::Initialize(SpriteRenderer *spriteRenderer,
                        const std::string &textureFilePath) {

  if (!spriteRenderer) {
    Logger::Log("[Sprite] Initialize: spriteRenderer is null\n");
    assert(false && "Sprite::Initialize: spriteRenderer is null");
    return;
  }

  spriteRenderer_ = spriteRenderer;

  dx12Core_ = spriteRenderer->GetDx12Core();

  if (!dx12Core_) {
    Logger::Log("[Sprite] Initialize: dx12Core is null\n");
    assert(false && "Sprite::Initialize: dx12Core is null");
    return;
  }

  // テクスチャをロードしてファイルパスを保持
  TextureManager::GetInstance()->LoadTexture(textureFilePath);

  textureFilePath_ = textureFilePath;

  CreateVertexData();

  CreateMaterialData();

  CreateTransformationMatrixData();

  CreateUIEffectParamsData();

  AdjustTextureSize();
}

void Sprite::Update(Transform uvTransform) {

  assert(dx12Core_ && "Sprite::Update: not initialized");
  assert(vertexData && "Sprite::Update: vertexData is null");
  assert(materialData && "Sprite::Update: materialData is null");
  assert(transformationMatrixData &&
         "Sprite::Update: transformationMatrixData is null");

  // アンカーポイントを設定
  float left = 0.0f - anchorPoint.x;
  float right = 1.0f - anchorPoint.x;
  float top = 0.0f - anchorPoint.y;
  float bottom = 1.0f - anchorPoint.y;

  // 左右反転
  if (isFlipX) {
    left = -left;
    right = -right;
  }

  // 上下反転
  if (isFlipY) {
    top = -top;
    bottom = -bottom;
  }

  const DirectX::TexMetadata &metadata =
      TextureManager::GetInstance()->GetMetaData(textureFilePath_);

  float texLeft = textureLeftTop.x / metadata.width;
  float texRight = (textureLeftTop.x + textureSize.x) / metadata.width;
  float texTop = textureLeftTop.y / metadata.height;
  float texBottom = (textureLeftTop.y + textureSize.y) / metadata.height;

  // 頂点リソースにデータを書き込む(4点分)

  // 一枚目の三角形
  vertexData[0].position = {left, bottom, 0.0f, 1.0f}; // 左
  vertexData[0].texcoord = {texLeft, texBottom};
  vertexData[1].position = {left, top, 0.0f, 1.0f}; // 左上
  vertexData[1].texcoord = {texLeft, texTop};
  vertexData[2].position = {right, bottom, 0.0f, 1.0f}; // 右下
  vertexData[2].texcoord = {texRight, texBottom};

  // 二枚目
  vertexData[3].position = {right, top, 0.0f, 1.0f}; // 右上
  vertexData[3].texcoord = {texRight, texTop};

  Vector3 finalScale = {size_.x * transform_.scale.x,
                        size_.y * transform_.scale.y, 1.0f};

  // TransformからWorldMatrixを作る
  Matrix4x4 worldMatrix =
      MakeAffineMatrix(finalScale, transform_.rotate, transform_.translate);

  // ViewMatrixを作って単位行列を代入
  Matrix4x4 viewMatrix = MakeIdentity4x4();

  // ProjectionMatrixを作って並列投影行列を書き込む
  Matrix4x4 projectionMatrix =
      MakeOrthographicMatrix(0.0f, 0.0f, float(WindowSystem::kClientWidth),
                             float(WindowSystem::kClientHeight), 0.0f, 100.0f);

  // Sprite用のWorldViewProjectionMatrixを作る
  Matrix4x4 worldViewProjectionMatrix =
      Multiply(Multiply(worldMatrix, viewMatrix), projectionMatrix);

  // uvTransformの情報
  Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransform.scale);
  uvTransformMatrix =
      Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransform.rotate.z));
  uvTransformMatrix =
      Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransform.translate));
  materialData->uvTransform = uvTransformMatrix;

  // データに代入
  transformationMatrixData->WVP = worldViewProjectionMatrix;
}

void Sprite::Draw() {

  assert(dx12Core_ && "Sprite::Draw: not initialized");
  assert(materialResource && "Sprite::Draw: materialResource is null");
  assert(transformationMatrixResource &&
         "Sprite::Draw: transformationMatrixResource is null");
  assert(vertexResource && "Sprite::Draw: vertexResource is null");
  assert(indexResource && "Sprite::Draw: indexResource is null");

  ID3D12GraphicsCommandList *commandList_ = dx12Core_->GetCommandList();

  // VertexBufferViewを設定
  commandList_->IASetVertexBuffers(0, 1,
                                   &vertexBufferView); // VBVを設定

  // IndexBufferViewを設定
  commandList_->IASetIndexBuffer(&indexBufferView); // IBVを設定

  // マテリアルCBufferの場所を設定。球とは別のマテリアルを使う
  commandList_->SetGraphicsRootConstantBufferView(
      0, materialResource->GetGPUVirtualAddress());

  // TransformationMatrixCBufferの場所を設定
  commandList_->SetGraphicsRootConstantBufferView(
      1, transformationMatrixResource->GetGPUVirtualAddress());

  // SRVのDescriptorTableの先頭を設定(描画に使うテクスチャの設定)
  commandList_->SetGraphicsRootDescriptorTable(
      2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));

  // 描画(ドローコール)
  commandList_->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::DrawUIEffect() {
  assert(dx12Core_ && "Sprite::DrawUIEffect: not initialized");
  assert(materialResource && "Sprite::DrawUIEffect: materialResource is null");
  assert(transformationMatrixResource && "Sprite::DrawUIEffect: transformationMatrixResource is null");
  assert(vertexResource && "Sprite::DrawUIEffect: vertexResource is null");
  assert(indexResource && "Sprite::DrawUIEffect: indexResource is null");
  assert(uiEffectParamsResource_ && "Sprite::DrawUIEffect: uiEffectParamsResource is null");

  ID3D12GraphicsCommandList *commandList_ = dx12Core_->GetCommandList();

  commandList_->IASetVertexBuffers(0, 1, &vertexBufferView);
  commandList_->IASetIndexBuffer(&indexBufferView);

  // b0: Material
  commandList_->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

  // b0 (VS): TransformationMatrix
  commandList_->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());

  // t0: Texture
  commandList_->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_));

  // b1: UIEffectParams
  commandList_->SetGraphicsRootConstantBufferView(3, uiEffectParamsResource_->GetGPUVirtualAddress());

  commandList_->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::SetUIEffectParams(float time, int effectType, float splitY, float amplitude, float frequency, float speed) {
  if (uiEffectParamsData_) {
    uiEffectParamsData_->time = time;
    uiEffectParamsData_->effectType = effectType;
    uiEffectParamsData_->splitY = splitY;
    uiEffectParamsData_->amplitude = amplitude;
    uiEffectParamsData_->frequency = frequency;
    uiEffectParamsData_->speed = speed;
  }
}

void Sprite::SetDissolveParams(float progress, float edgeWidth, bool flip,
                               float glowStrength, bool descend) {
  if (uiEffectParamsData_) {
    uiEffectParamsData_->progress = progress;
    uiEffectParamsData_->edgeWidth = edgeWidth;
    uiEffectParamsData_->dissolveFlip = flip ? 1.0f : 0.0f;
    uiEffectParamsData_->glowStrength = glowStrength;
    uiEffectParamsData_->dissolveDescend = descend ? 1.0f : 0.0f;
  }
}

void Sprite::CreateVertexData() {

  // VertexResourceを作る
  //=========================
  vertexResource = dx12Core_->CreateBufferResource(sizeof(VertexData) * 4);

  // IndexResourceを作る
  //=========================
  indexResource = dx12Core_->CreateBufferResource(sizeof(uint32_t) * 6);

  // VertexBufferViewを作る(値を設定するだけ)
  //=======================================
  // リソースの先頭アドレスから使う
  vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

  // 使用するリソースサイズは頂点六つ分のサイズ
  vertexBufferView.SizeInBytes = sizeof(VertexData) * 4;

  // 1頂点あたりのサイズ
  vertexBufferView.StrideInBytes = sizeof(VertexData);

  // IndexBufferViewを作る(値を設定するだけ)
  //=======================================
  // リソースの先頭アドレスから使う
  indexBufferView.BufferLocation = indexResource->GetGPUVirtualAddress();

  // 使用するリソースのサイズはインデックス六つ分
  indexBufferView.SizeInBytes = sizeof(uint32_t) * 6;

  // インデックスはuint32_tとする
  indexBufferView.Format = DXGI_FORMAT_R32_UINT;

  // VertexResourceにデータを書き込むためのアドレスを取得してvertexDataに割り当てる
  //==========================================================================
  // 頂点データ設定
  HRESULT hr =
      vertexResource->Map(0, nullptr, reinterpret_cast<void **>(&vertexData));
  assert(SUCCEEDED(hr));
  assert(vertexData);

  // IndexResourceにデータを書き込むためのアドレスを取得してindexDataに割り当てる
  //==========================================================================
  hr = indexResource->Map(0, nullptr, reinterpret_cast<void **>(&indexData));
  assert(SUCCEEDED(hr));
  assert(indexData);

  // インデックスリソースにデータを書き込む(6点分)
  indexData[0] = 0; // 左下
  indexData[1] = 1; // 左上
  indexData[2] = 2; // 右下
  indexData[3] = 1;
  indexData[4] = 3; // 右上
  indexData[5] = 2;
}

void Sprite::CreateMaterialData() {

  UINT materialSize = (sizeof(Material) + 255) & ~255;

  // リソースを作る
  materialResource = dx12Core_->CreateBufferResource(materialSize);

  // 書き込むためのアドレスを取得
  HRESULT hr = materialResource->Map(0, nullptr,
                                     reinterpret_cast<void **>(&materialData));
  assert(SUCCEEDED(hr));
  assert(materialData);

  // マテリアルデータの初期値を書き込む
  materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
  materialData->uvTransform = MakeIdentity4x4();
}

void Sprite::CreateTransformationMatrixData() {

  // リソースサイズ
  UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

  // リソースを作る
  transformationMatrixResource =
      dx12Core_->CreateBufferResource(transformationMatrixSize);

  // 書き込むためのアドレスを取得
  HRESULT hr = transformationMatrixResource->Map(
      0, nullptr, reinterpret_cast<void **>(&transformationMatrixData));
  assert(SUCCEEDED(hr));
  assert(transformationMatrixData);

  // 単位行列を書き込んでおく
  transformationMatrixData->WVP = MakeIdentity4x4();
}

void Sprite::CreateUIEffectParamsData() {
  // バッファリソースを作る
  uiEffectParamsResource_ = dx12Core_->CreateBufferResource(sizeof(UIEffectParams));
  // データを書き込むためのアドレスを取得して割り当てる
  HRESULT hr = uiEffectParamsResource_->Map(0, nullptr, reinterpret_cast<void **>(&uiEffectParamsData_));
  assert(SUCCEEDED(hr));
  assert(uiEffectParamsData_);

  // デフォルト値を設定
  uiEffectParamsData_->time = 0.0f;
  uiEffectParamsData_->effectType = 0; // 通常
  uiEffectParamsData_->splitY = 0.5f;
  uiEffectParamsData_->amplitude = 0.05f;
  uiEffectParamsData_->frequency = 20.0f;
  uiEffectParamsData_->speed = 5.0f;
  uiEffectParamsData_->blurWidth = 0.05f;
  uiEffectParamsData_->boundaryAmp = 0.03f;
  uiEffectParamsData_->progress = 0.0f;
  uiEffectParamsData_->edgeWidth = 0.05f;
  uiEffectParamsData_->dissolveFlip = 0.0f;
  uiEffectParamsData_->glowStrength = 1.0f;
  uiEffectParamsData_->dissolveDescend = 0.0f;
}

void Sprite::ChangeTexture(const std::string &textureFilePath) {

  // まだ読み込まれていないテクスチャであれば読み込む
  TextureManager::GetInstance()->LoadTexture(textureFilePath);

  // テクスチャの差し替え
  textureFilePath_ = textureFilePath;

  AdjustTextureSize();
}

void Sprite::AdjustTextureSize() {

  // テクスチャデータを取得
  const DirectX::TexMetadata &metadata =
      TextureManager::GetInstance()->GetMetaData(textureFilePath_);

  textureSize.x = static_cast<float>(metadata.width);
  textureSize.y = static_cast<float>(metadata.height);

  size_ = textureSize;
}
