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

// 2つの種 pi, pj の垂直二等分面（中点を通り、pj→pi 向きの法線）
// pi に近い側が表になるので、ClipMesh に渡すと pi 側が残る
Plane MakeBisectorPlane(const Vector3 &pi, const Vector3 &pj);

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

// ボロノイ分割で src を破片に分ける（種1つにつき破片1つ）
// 各破片は、他のすべての種との二等分面で src を切って作る
// 種がメッシュから遠く、何も残らなかった破片は含まない
std::vector<Model::ModelData>
VoronoiFracture(const Model::ModelData &src, const std::vector<Vector3> &seeds);

// 閉じたメッシュの体積（検証用: 破片の体積の合計 = 元の体積 になるか確認する）
float ComputeVolume(const Model::ModelData &mesh);

// 閉じたメッシュの重心（体積で重み付けするので、頂点の数や偏りに左右されない）
Vector3 ComputeCentroid(const Model::ModelData &mesh);

// メッシュの原点を重心に移す（全頂点から重心を引く）
// 戻り値は移す前の重心。表示するときはこの位置に置けば元と同じ見た目になる
// （原点が重心にあると、回転させたときに破片がその場で回る）
Vector3 CenterMesh(Model::ModelData &mesh);

// メッシュを囲む箱（AABB）の最小点・最大点を求める（種をばらまく範囲に使う）
void ComputeBounds(const Model::ModelData &mesh, Vector3 &outMin,
                   Vector3 &outMax);

// 種の偏らせ方
// 種が密集した所は細かい破片、まばらな所は大きな破片になる
// 箱の表面の1点を「衝撃点」とし、そこへ種を寄せて
// 「撃たれた所が粉々、離れるほど大きな塊」にする
struct SeedBias {
  // false なら偏らせず、箱全体に均等に置く
  bool enabled = false;
  // 種が衝撃点の近くに置かれる確率（0〜1）
  // 残りは箱全体に均等に置き、大きな塊を担当する
  float ratio = 0.5f;
  // 衝撃点からの範囲（箱の対角線の長さに対する割合）
  float radius = 0.4f;
  // 集中度。距離を「範囲 x 乱数^集中度」で決めるので、
  // 大きいほど衝撃点に吸い寄せられる
  float falloff = 2.0f;
  // 種同士の最小の間隔（箱の対角線の長さに対する割合）
  // 種が近すぎると、その間に目に見えないほど薄い破片ができるのを防ぐ
  float minSpacing = 0.05f;
};

// 箱 [min, max] の中にランダムに種を count 個置く
// randomSeed が同じなら毎回同じ配置になる（見た目の再現・デバッグのため）
// outImpactPoint を渡すと、偏らせたときの衝撃点を受け取れる（確認表示用）
std::vector<Vector3> GenerateSeeds(const Vector3 &min, const Vector3 &max,
                                   uint32_t count, uint32_t randomSeed,
                                   const SeedBias &bias = {},
                                   Vector3 *outImpactPoint = nullptr);

} // namespace MeshFracture
