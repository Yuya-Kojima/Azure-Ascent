#pragma once
#include "Core/WindowSystem.h"
#include "Math/Matrix4x4.h"
#include "Math/Transform.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <cassert>
#include <d3d12.h>
#include <string>
#include <wrl.h>

class Dx12Core;

class SpriteRenderer;

class Sprite {

  struct VertexData {
    Vector4 position;
    Vector2 texcoord;
  };

  struct Material {
    Vector4 color;
    Matrix4x4 uvTransform;
  };

  struct TransformationMatrix {
    Matrix4x4 WVP;
  };

  struct UIEffectParams {
    float time;
    int effectType;
    float splitY;
    float amplitude;
    float frequency;
    float speed;
    float blurWidth;    // 波エフェクトの横方向ブラー幅
    float boundaryAmp;  // 波エフェクトの境界線の波打ち幅
    float progress;     // ディゾルブの進行度（しきい値）0〜1
    float edgeWidth;    // ディゾルブの縁を光らせる幅（しきい値と比べる値の範囲）
    float dissolveFlip; // ディゾルブの縦方向を反転するか（0 = しない、1 = する）
    float glowStrength; // ディゾルブの縁の光の強さ 0〜1
  };

public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer,
                  const std::string &textureFilePath);

  /// <summary>
  /// 更新処理
  /// </summary>
  void Update(Transform uvTransform);

  /// <summary>
  /// 描画処理
  /// </summary>
  void Draw();

  /// <summary>
  /// UIエフェクト用の描画処理
  /// </summary>
  void DrawUIEffect();

  /// <summary>
  /// UIEffectのパラメータを設定する
  /// </summary>
  void SetUIEffectParams(float time, int effectType, float splitY = 0.5f, float amplitude = 0.05f, float frequency = 20.0f, float speed = 5.0f);

  /// <summary>
  /// ディゾルブ（ノイズ＋しきい値）エフェクトのパラメータを設定する
  /// </summary>
  void SetDissolveParams(float progress, float edgeWidth, bool flip = false,
                         float glowStrength = 1.0f);

private:
  SpriteRenderer *spriteRenderer_ = nullptr;

  Dx12Core *dx12Core_ = nullptr;

  // テクスチャ番号
  // uint32_t textureIndex_ = 0;

  // テクスチャのファイルパス
  std::string textureFilePath_;

  // D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU_{};

private:
  /* 頂点データ
  -----------------------*/

  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> indexResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  VertexData *vertexData = nullptr;
  uint32_t *indexData = nullptr;

  // バッファリソースの使い道を補足するバッファビュー
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
  D3D12_INDEX_BUFFER_VIEW indexBufferView{};

  /* マテリアルデータ
  ----------------------------*/

  // マテリアルバッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  Material *materialData = nullptr;

  /* 座標変換行列データ
  -----------------------------*/

  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  TransformationMatrix *transformationMatrixData = nullptr;

  /// <summary>
  /// 頂点データを作成
  /// </summary>
  void CreateVertexData();

  /// <summary>
  /// マテリアルデータを作成
  /// </summary>
  void CreateMaterialData();

  /// <summary>
  /// 座標変換行列データを生成
  /// </summary>
  void CreateTransformationMatrixData();

  /// <summary>
  /// UIEffectデータを生成
  /// </summary>
  void CreateUIEffectParamsData();

  // UIEffectのバッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> uiEffectParamsResource_ = nullptr;
  UIEffectParams *uiEffectParamsData_ = nullptr;

private:
  Transform transform_{
      {1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  // アンカーポイント
  Vector2 anchorPoint = {0.0f, 0.0f};

  Vector2 size_{100.0f, 100.0f};

public:
  /// <summary>
  /// positionのゲッター
  /// </summary>
  /// <returns></returns>
  Vector2 GetPosition() const {
    return {transform_.translate.x, transform_.translate.y};
  }

  /// <summary>
  /// positionのセッター
  /// </summary>
  /// <param name="position"></param>
  void SetPosition(const Vector2 &position) {
    transform_.translate.x = position.x;
    transform_.translate.y = position.y;
  }

  /// <summary>
  /// rotationのゲッター
  /// </summary>
  /// <returns></returns>
  float GetRotation() const { return transform_.rotate.z; }

  /// <summary>
  /// rotationのセッター
  /// </summary>
  /// <param name="rotation"></param>
  void SetRotation(float rotation) { transform_.rotate.z = rotation; }

  /// <summary>
  /// scale のゲッター（倍率）
  /// </summary>
  Vector2 GetScale() const { return {transform_.scale.x, transform_.scale.y}; }

  /// <summary>
  /// scale のセッター（倍率）
  /// </summary>
  void SetScale(const Vector2 &scale) {
    transform_.scale = {scale.x, scale.y, 1.0f};
  }

  /// <summary>
  /// colorのゲッター
  /// </summary>
  /// <param name="rotation"></param>
  Vector4 GetColor() const {
    return materialData ? materialData->color : Vector4(1, 1, 1, 1);
  }

  /// <summary>
  /// colorのセッター
  /// </summary>
  /// <param name="rotation"></param>
  void SetColor(const Vector4 &color) {
    assert(materialData);
    materialData->color = color;
  }

  ///// <summary>
  ///// sizeのゲッター
  ///// </summary>
  ///// <returns></returns>
  // Vector2 GetSize() const { return {transform_.scale.x, transform_.scale.y};
  // }

  ///// <summary>
  ///// sizeのセッター
  ///// </summary>
  ///// <param name="position"></param>
  // void SetSize(const Vector2 &size) {
  //   transform_.scale = {size.x, size.y, 1.0f};
  // }

  Vector2 GetSize() const { return size_; }

  void SetSize(const Vector2 &size) { size_ = size; }

  /// <summary>
  /// アンカーポイントのゲッター
  /// </summary>
  /// <returns></returns>
  Vector2 GetAnchorPoint() const { return anchorPoint; }

  /// <summary>
  /// アンカーポイントのセッター
  /// </summary>
  /// <param name="anchorPoint"></param>
  void SetAnchorPoint(Vector2 anchorPoint) { this->anchorPoint = anchorPoint; }

  /// <summary>
  /// 任意のタイミングでテクスチャを変更する
  /// </summary>
  /// <param name="textureFilePath"></param>
  void ChangeTexture(const std::string &textureFilePath);

  // フリップのゲッター
  bool GetIsFlipX() const { return isFlipX; }
  bool GetIsFlipY() const { return isFlipY; }

  // フリップのセッター
  void SetIsFlipX(const bool isFlipX) { this->isFlipX = isFlipX; }
  void SetIsFlipY(const bool isFlipY) { this->isFlipY = isFlipY; }

  /// <summary>
  /// テクスチャ左上座標ゲッター
  /// </summary>
  /// <returns></returns>
  Vector2 GetTextureLeftTop() const { return textureLeftTop; }

  /// <summary>
  /// テクスチャ左上座標セッター
  /// </summary>
  /// <param name="textureLeftTop"></param>
  void SetTextureLeftTop(Vector2 textureLeftTop) {
    this->textureLeftTop = textureLeftTop;
  }

  /// <summary>
  /// テクスチャ切り出しサイズゲッター
  /// </summary>
  /// <returns></returns>
  Vector2 GetTextureSize() const { return textureSize; }

  /// <summary>
  /// テクスチャ切り出しサイズセッター
  /// </summary>
  /// <param name="textureSize"></param>
  void SetTextureSize(Vector2 textureSize) { this->textureSize = textureSize; }

private:
  // 左右フリップ
  bool isFlipX = false;

  // 上下フリップ
  bool isFlipY = false;

private:
  // テクスチャ左上座標
  Vector2 textureLeftTop{0.0f, 0.0f};

  // テクスチャ切り出しサイズ
  Vector2 textureSize{100.0f, 100.0f};

  // テクスチャサイズをイメージに合わせる
  void AdjustTextureSize();
};
