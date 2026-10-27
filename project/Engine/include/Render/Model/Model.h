#pragma once
#include "Animation.h"
#include "Math/MathUtil.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <map>
#include <wrl.h>

class ModelRenderer;
class Dx12Core;
struct SkinCluster;

class Model {

public:
  struct VertexData {
    Vector4 position;
    Vector2 texcoord;
    Vector3 normal;
  };
  struct MaterialData {
    std::string textureFilePath;
    // uint32_t textureIndex = 0;
  };

public:
  struct Node {
    QuaternionTransform transform;
    Matrix4x4 localMatrix;
    std::string name;
    std::vector<Node> children;
  };

public:
  struct VertexWeightData {
      float weight;
      uint32_t vertexIndex;
  };

  struct JointWeightData {
      Matrix4x4 inverseBindPoseMatrix;
      std::vector<VertexWeightData> vertexWeights;
  };

  struct ModelData {
    std::vector<VertexData> vertices;
    std::vector<uint32_t> indices;
    MaterialData material;
    Node rootNode;
    std::map<std::string, JointWeightData> skinClusterData;
  };

public:
  struct Material {
    Vector4 color;
    int32_t enableLighting;
    float padding[3];
    Matrix4x4 uvTransform;
    float shininess;
    float environmentCoefficient;
    int32_t enableDissolve;
    float dissolveThreshold;
    float dissolveEdgeRange;
    Vector2 maskTransform;
    float padding2;
    Vector4 dissolveEdgeColor;
    // リムライト（縁の発光）: rgb=色, w=強度(0で無効)
    Vector4 rimColor = {0.0f, 0.0f, 0.0f, 0.0f};
    float rimPower = 3.0f; // 大きいほど縁の細い範囲だけ光る
    float backRimScale = 0.0f; // 平行光源の逆光の縁を掛ける倍率（0で無効）
    float specularScale = 1.0f; // 鏡面反射の倍率（0で鏡面反射なし）
    float padding3 = 0.0f;
    // 模様の光（テクスチャの水色の所に足す光）: rgb=色, w=強さ(0で無効)
    Vector4 patternGlow = {0.0f, 0.0f, 0.0f, 0.0f};
    // 模様とみなす水色らしさ（min(緑, 青) − 赤。リニアの色）: x=光り始める値, y=光りきる値, zw=未使用
    Vector4 patternRange = {0.45f, 0.6f, 0.0f, 0.0f};
    // 傷の中心: xyz=ワールド座標, w=穴の半径（ワールドの長さ）
    Vector4 woundCenter = {0.0f, 0.0f, 0.0f, 0.0f};
    // 傷の外向きの向き: xyz=単位ベクトル, w=ひびの長さ（中心から。ワールドの長さ）
    Vector4 woundNormal = {0.0f, 1.0f, 0.0f, 0.0f};
    // 傷の面に沿った向き（ひびの向きの基準）: xyz=単位ベクトル, w=開いた割合（0で傷なし、1で開ききる）
    Vector4 woundTangent = {1.0f, 0.0f, 0.0f, 0.0f};
    // 傷から漏れる光: rgb=色, w=強さ（0で穴とひびは暗いまま）
    Vector4 woundGlow = {0.0f, 0.0f, 0.0f, 0.0f};
    // 傷の形: x=ひびの根元の太さ（ワールドの長さ）, y=縁の欠けた殻の幅（穴の半径に対する割合）, z=縁のぎざぎざの深さ（穴の半径に対する割合）, w=周りの面ににじむ光の強さ
    Vector4 woundShape = {0.0f, 0.0f, 0.0f, 0.0f};
  };

  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(ModelRenderer *modelRenderer,
                  const std::string &directorypath,
                  const std::string &filename);

  /// <summary>
  /// 頂点データから初期化（Primitive用）
  /// </summary>
  void InitializeFromVertices(ModelRenderer *modelRenderer,
                              const std::vector<VertexData> &vertices);

  /// <summary>
  /// ModelDataから直接初期化（プロシージャル・メッシュ生成用）
  /// </summary>
  void InitializeFromModelData(ModelRenderer *modelRenderer,
                              const ModelData &modelData);

  /// <summary>
  /// 描画
  /// </summary>
  void Draw(const SkinCluster* skinCluster = nullptr);

private:
  ModelRenderer *modelRenderer_;

  Dx12Core *dx12Core_ = nullptr;

  // Objファイルのデータ
  ModelData modelData_;

  /// <summary>
  /// mtlファイルを読む
  /// </summary>
  /// <param name="directoryPath"></param>
  /// <param name="filename"></param>
  /// <returns></returns>
  static MaterialData LoadMaterialTemplateFile(const std::string &directoryPath,
                                               const std::string &filename);

public:
  /// <summary>
  /// Objファイルを読む
  /// </summary>
  /// <param name="directoryPath"></param>
  /// <param name="filename"></param>
  /// <returns></returns>
  void LoadModelFile(const std::string &directoryPath,
                     const std::string &filename);

private:
  /* 頂点データ
  -----------------------------*/

  // バッファリソース
  Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;

  // バッファリソース内のデータを指すポインタ
  VertexData *vertexData = nullptr;

  // バッファリソースの使い道を補足するバッファビュー
  D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

  /* インデックスデータ
  -----------------------------*/
  Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_ = nullptr;
  D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

  /// <summary>
  /// 頂点データを生成
  /// </summary>
  void CreateVertexData();

private:
  Material defaultMaterial_;

public:
  const Material &GetDefaultMaterial() const { return defaultMaterial_; }
  ID3D12Resource *GetVertexResource() const { return vertexResource.Get(); }

private:
  Node ReadNode(aiNode *node);

public:
  const Matrix4x4 &GetRootLocalMatrix() const {
    return modelData_.rootNode.localMatrix;
  }

  const Node &GetRootNode() const { return modelData_.rootNode; }
  const ModelData &GetModelData() const { return modelData_; }
};

Animation LoadAnimationFile(const std::string &directoryPath,
                            const std::string &filename);

Vector3 CalculateValue(const std::vector<KeyframeVector3> &keyframes,
                       float time);
Quaternion CalculateValue(const std::vector<KeyframeQuaternion> &keyframes,
                          float time);
