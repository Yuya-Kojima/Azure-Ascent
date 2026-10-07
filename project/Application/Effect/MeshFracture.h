#pragma once
#include "Math/Vector3.h"
#include "Render/Model/Model.h"
#include <vector>

/// <summary>
/// メッシュ破砕（撃破演出）の計算部分
/// CPUだけで完結し、GPUリソースには一切触れない（テスト・数値検証しやすくするため）
/// </summary>
namespace MeshFracture {

// 平面: n・x + d = 0（normal は長さ1）
struct Plane {
  Vector3 normal;
  float d;
};

// 点 point を通り、法線 normal を持つ平面を作る（d = -(n・p0)）
Plane MakePlane(const Vector3 &normal, const Vector3 &point);

// 平面を裏返す（反対側の破片を作るとき用）
Plane FlipPlane(const Plane &plane);

// 符号付き距離 s = n・p + d（s > 0 で表側、s < 0 で裏側）
float SignedDistance(const Plane &plane, const Vector3 &point);

// 辺 a→b と平面の交点にあたる頂点を作る（位置・UV・法線を同じ t で補間）
// sa, sb は a, b の符号付き距離。符号が異なるときだけ呼ぶこと
Model::VertexData IntersectEdge(const Model::VertexData &a,
                                const Model::VertexData &b, float sa, float sb);

// 凸多角形を平面で切り、表側だけを残す（Sutherland-Hodgman）
// 頂点の並び順（巻き順）は元の多角形のまま保たれる
// outCutPoints を渡すと、平面上にできた点（キャップ用）を追加する
std::vector<Model::VertexData>
ClipPolygon(const std::vector<Model::VertexData> &polygon, const Plane &plane,
            std::vector<Vector3> *outCutPoints = nullptr);

// メッシュを平面で切り、表側の破片を作る。切り口にはキャップ（フタ）を貼る
// 元のメッシュは凸で閉じていること
Model::ModelData ClipMesh(const Model::ModelData &src, const Plane &plane);

// 閉じたメッシュの体積（検証用: 破片の体積の合計 = 元の体積 になるか確認する）
float ComputeVolume(const Model::ModelData &mesh);

} // namespace MeshFracture
