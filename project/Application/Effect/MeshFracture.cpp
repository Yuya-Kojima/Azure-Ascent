#include "MeshFracture.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>

namespace MeshFracture {

namespace {

// 平面上とみなす距離の許容誤差
const float kOnPlaneEpsilon = 1e-5f;
// キャップの点を同一点とみなす距離（の2乗）
const float kMergeDistanceSq = 1e-8f;

Vector3 ToVector3(const Vector4 &v) { return {v.x, v.y, v.z}; }

// 断面の点を角度順に並べ、扇形に三角形分割してキャップを貼る
void BuildCap(std::vector<Vector3> points, const Plane &plane,
              Model::ModelData &out) {
  // 1.
  // 重複を消す（隣り合う三角形は辺を共有しているので、同じ交点が2回出てくる）
  std::vector<Vector3> unique;
  for (const Vector3 &p : points) {
    bool isDuplicate = false;
    for (const Vector3 &q : unique) {
      if (LengthSq(p - q) < kMergeDistanceSq) {
        isDuplicate = true;
        break;
      }
    }
    if (!isDuplicate) {
      unique.push_back(p);
    }
  }
  if (unique.size() < 3) {
    return; // 面にならない（平面がメッシュをかすめただけ）
  }

  // 2. 重心（凸多角形の必ず内側にあるので、角度で並べる基準になる）
  Vector3 center = {0.0f, 0.0f, 0.0f};
  for (const Vector3 &p : unique) {
    center += p;
  }
  center = center * (1.0f / static_cast<float>(unique.size()));

  // 3. 平面上の2軸 (u, v) を作り、重心から見た角度で並べる
  //    法線とほぼ平行にならない補助ベクトルを選んでから外積で作る
  const Vector3 &n = plane.normal;
  Vector3 helper = (std::abs(n.y) < 0.99f) ? Vector3{0.0f, 1.0f, 0.0f}
                                           : Vector3{1.0f, 0.0f, 0.0f};
  Vector3 u = SafeNormalize(Cross(helper, n));
  Vector3 v = Cross(n, u);
  std::sort(unique.begin(), unique.end(),
            [&](const Vector3 &a, const Vector3 &b) {
              Vector3 da = a - center;
              Vector3 db = b - center;
              return std::atan2(Dot(da, v), Dot(da, u)) <
                     std::atan2(Dot(db, v), Dot(db, u));
            });

  // 4. 扇形に三角形分割する
  //    角度の昇順は u×v = n
  //    方向から見て反時計回り。キャップは表側の破片の切り口なので -n
  //    を向く必要があり、(重心, 次の点, 今の点) の順にすると外積が -n を向く
  const Vector3 capNormal = -n;
  auto makeCapVertex = [&](const Vector3 &p) {
    Vector3 local = p - center;
    Model::VertexData vtx;
    vtx.position = {p.x, p.y, p.z, 1.0f};
    vtx.texcoord = {Dot(local, u),
                    Dot(local, v)}; // 断面には元のUVがないので平面に投影
    vtx.normal = capNormal; // 側面とは法線が違うので、頂点は共有せず別に作る
    return vtx;
  };
  const size_t count = unique.size();
  for (size_t i = 0; i < count; ++i) {
    const Vector3 &current = unique[i];
    const Vector3 &next = unique[(i + 1) % count];
    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    out.vertices.push_back(makeCapVertex(center));
    out.vertices.push_back(makeCapVertex(next));
    out.vertices.push_back(makeCapVertex(current));
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
  }
}

} // namespace

Plane MakePlane(const Vector3 &normal, const Vector3 &point) {
  Vector3 n = SafeNormalize(normal);
  return {n, -Dot(n, point)};
}

Plane FlipPlane(const Plane &plane) { return {-plane.normal, -plane.d}; }

float SignedDistance(const Plane &plane, const Vector3 &point) {
  return Dot(plane.normal, point) + plane.d;
}

Model::VertexData IntersectEdge(const Model::VertexData &a,
                                const Model::VertexData &b, float sa,
                                float sb) {
  // a から何割進んだところで平面と交わるか（0〜1）
  // sa と sb は符号が異なるので、分母 sa - sb が 0 になることはない
  const float t = sa / (sa - sb);

  // 位置・UV・法線を同じ t で補間する
  Model::VertexData p;
  Vector3 position = Lerp(ToVector3(a.position), ToVector3(b.position), t);
  p.position = {position.x, position.y, position.z, 1.0f};
  p.texcoord = {Lerp(a.texcoord.x, b.texcoord.x, t),
                Lerp(a.texcoord.y, b.texcoord.y, t)};
  // 補間すると長さが1より短くなるので、正規化して戻す
  p.normal = Normalize(Lerp(a.normal, b.normal, t));
  return p;
}

std::vector<Model::VertexData>
ClipPolygon(const std::vector<Model::VertexData> &polygon, const Plane &plane,
            std::vector<Vector3> *outCutPoints) {
  std::vector<Model::VertexData> result;
  const size_t count = polygon.size();
  if (count == 0) {
    return result;
  }

  // 各辺（今の点 → 次の点）を順番に見ていく
  for (size_t i = 0; i < count; ++i) {
    const Model::VertexData &current = polygon[i];
    const Model::VertexData &next = polygon[(i + 1) % count];
    float sCurrent = SignedDistance(plane, ToVector3(current.position));
    float sNext = SignedDistance(plane, ToVector3(next.position));
    bool isCurrentFront = (sCurrent >= 0.0f);
    bool isNextFront = (sNext >= 0.0f);

    // 今の点が表なら残す
    if (isCurrentFront) {
      result.push_back(current);
      // ちょうど平面上の点は断面の輪郭にもなる
      if (outCutPoints && std::abs(sCurrent) < kOnPlaneEpsilon) {
        outCutPoints->push_back(ToVector3(current.position));
      }
    }

    // 表と裏をまたぐ辺なら、交点を追加する
    if (isCurrentFront != isNextFront) {
      Model::VertexData cut = IntersectEdge(current, next, sCurrent, sNext);
      result.push_back(cut);
      if (outCutPoints) {
        outCutPoints->push_back(ToVector3(cut.position));
      }
    }
  }
  return result;
}

Model::ModelData ClipMesh(const Model::ModelData &src, const Plane &plane) {
  Model::ModelData out;
  out.material = src.material;
  out.rootNode = src.rootNode;

  std::vector<Vector3> cutPoints;
  for (size_t i = 0; i + 2 < src.indices.size(); i += 3) {
    std::vector<Model::VertexData> triangle = {
        src.vertices[src.indices[i + 0]],
        src.vertices[src.indices[i + 1]],
        src.vertices[src.indices[i + 2]],
    };
    std::vector<Model::VertexData> polygon =
        ClipPolygon(triangle, plane, &cutPoints);
    if (polygon.size() < 3) {
      continue; // 全部が裏側だった
    }

    // 切った結果（3〜4角形）を、元の巻き順のまま扇形に三角形分割する
    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    out.vertices.insert(out.vertices.end(), polygon.begin(), polygon.end());
    for (uint32_t k = 1; k + 1 < polygon.size(); ++k) {
      out.indices.push_back(base);
      out.indices.push_back(base + k);
      out.indices.push_back(base + k + 1);
    }
  }

  BuildCap(cutPoints, plane, out);
  return out;
}

float ComputeVolume(const Model::ModelData &mesh) {
  // 原点と各三角形で作る四面体の符号付き体積を足し合わせる
  // （外側の四面体と内側の四面体が打ち消し合い、閉じたメッシュの体積だけが残る）
  float volume = 0.0f;
  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    Vector3 a = ToVector3(mesh.vertices[mesh.indices[i + 0]].position);
    Vector3 b = ToVector3(mesh.vertices[mesh.indices[i + 1]].position);
    Vector3 c = ToVector3(mesh.vertices[mesh.indices[i + 2]].position);
    volume += Dot(a, Cross(b, c)) / 6.0f;
  }
  return volume;
}

} // namespace MeshFracture
