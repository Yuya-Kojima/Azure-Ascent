#include "FracturePieceSet.h"
#include "Effect/MeshFracture.h"
#include "Math/MathUtil.h"
#include "Model/ModelManager.h"
#include <cmath>

namespace {
// 元のモデルの表面の上とみなす距離（元のモデルを囲む箱の対角線の長さに対する割合）
const float kOnSurfaceRate = 1e-4f;
// 同じ平面とみなす、法線の内積の下限
const float kSamePlaneDot = 0.9999f;

// 元のモデルの面が乗る平面（n・x + d = 0）
struct SurfacePlane {
  Vector3 normal;
  float d;
};

Vector3 ToVector3(const Vector4 &v) { return {v.x, v.y, v.z}; }

// 元のモデルの三角形から、面が乗る平面を重ならないように集める
std::vector<SurfacePlane> CollectSurfacePlanes(const Model::ModelData &source,
                                               float epsilon) {
  std::vector<SurfacePlane> planes;
  for (size_t i = 0; i + 2 < source.indices.size(); i += 3) {
    const Vector3 a = ToVector3(source.vertices[source.indices[i + 0]].position);
    const Vector3 b = ToVector3(source.vertices[source.indices[i + 1]].position);
    const Vector3 c = ToVector3(source.vertices[source.indices[i + 2]].position);
    const Vector3 cross = Cross(b - a, c - a);
    if (Length(cross) <= 0.0f) {
      continue;
    }
    const Vector3 normal = Normalize(cross);
    const float d = -Dot(normal, a);
    bool isDuplicate = false;
    for (const SurfacePlane &plane : planes) {
      if (Dot(plane.normal, normal) > kSamePlaneDot && std::abs(plane.d - d) < epsilon) {
        isDuplicate = true;
        break;
      }
    }
    if (!isDuplicate) {
      planes.push_back({normal, d});
    }
  }
  return planes;
}

// 点が乗っている元のモデルの平面を探す
const SurfacePlane *FindSurfacePlane(const std::vector<SurfacePlane> &planes,
                                     const Vector3 &point, float epsilon) {
  for (const SurfacePlane &plane : planes) {
    if (std::abs(Dot(plane.normal, point) + plane.d) < epsilon) {
      return &plane;
    }
  }
  return nullptr;
}

// 三角形 1 つを、頂点の位置を重心からの位置に直して足す
void AppendTriangle(Model::ModelData &out, const Model::VertexData (&triangle)[3],
                    const Vector3 &centroid) {
  for (const Model::VertexData &vertex : triangle) {
    Model::VertexData moved = vertex;
    moved.position.x -= centroid.x;
    moved.position.y -= centroid.y;
    moved.position.z -= centroid.z;
    out.indices.push_back(static_cast<uint32_t>(out.vertices.size()));
    out.vertices.push_back(moved);
  }
}
} // namespace

void FracturePieceSet::Create(const Model::ModelData &source,
                              uint32_t seedCount, uint32_t randomSeed,
                              const MeshFracture::SeedBias &seedBias) {
  pieces_.clear();

  // 元のモデルを囲む箱の中に種をばらまき、ボロノイ分割する
  Vector3 sourceMin, sourceMax;
  MeshFracture::ComputeBounds(source, sourceMin, sourceMax);
  const std::vector<Vector3> seeds = MeshFracture::GenerateSeeds(
      sourceMin, sourceMax, seedCount, randomSeed, seedBias);
  std::vector<Model::ModelData> meshes =
      MeshFracture::VoronoiFracture(source, seeds);

  // 飛ばす向きは「元の重心 → 破片の重心」（爆発したように外へ散る）
  const Vector3 sourceCentroid = MeshFracture::ComputeCentroid(source);

  // 三角形を外側の面と割れ口の面に分けるため、元のモデルの面が乗る平面を集める
  const float onSurfaceEpsilon = Length(sourceMax - sourceMin) * kOnSurfaceRate;
  const std::vector<SurfacePlane> surfacePlanes =
      CollectSurfacePlanes(source, onSurfaceEpsilon);

  pieces_.reserve(meshes.size());
  for (Model::ModelData &mesh : meshes) {
    // 頂点 0 個ではバッファを作れない（VoronoiFracture は空を返さないが念のため）
    if (mesh.vertices.empty()) {
      continue;
    }

    Piece piece;
    // 重心が破片を置く位置になる。外側の面・割れ口の面とも、原点を重心に移して作る
    piece.centroid = MeshFracture::ComputeCentroid(mesh);
    piece.direction = SafeNormalize(piece.centroid - sourceCentroid);

    Model::ModelData outer;
    Model::ModelData cap;
    outer.material = cap.material = mesh.material;
    outer.rootNode = cap.rootNode = mesh.rootNode;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
      const Model::VertexData triangle[3] = {mesh.vertices[mesh.indices[i + 0]],
                                             mesh.vertices[mesh.indices[i + 1]],
                                             mesh.vertices[mesh.indices[i + 2]]};
      const Vector3 corners[3] = {ToVector3(triangle[0].position),
                                  ToVector3(triangle[1].position),
                                  ToVector3(triangle[2].position)};
      // 三角形の重心が元のモデルの面の上にあれば外側の面、なければ割れ口の面
      const Vector3 center = (corners[0] + corners[1] + corners[2]) * (1.0f / 3.0f);
      if (FindSurfacePlane(surfacePlanes, center, onSurfaceEpsilon)) {
        AppendTriangle(outer, triangle, piece.centroid);
        continue;
      }
      AppendTriangle(cap, triangle, piece.centroid);

      // 割れ口の三角形の辺のうち、両端が元のモデルの同じ面に乗る辺が割れ目の線
      for (int edge = 0; edge < 3; ++edge) {
        const Vector3 &start = corners[edge];
        const Vector3 &end = corners[(edge + 1) % 3];
        if (Length(end - start) < onSurfaceEpsilon) {
          continue;
        }
        const SurfacePlane *plane = FindSurfacePlane(surfacePlanes, start, onSurfaceEpsilon);
        if (!plane || std::abs(Dot(plane->normal, end) + plane->d) >= onSurfaceEpsilon) {
          continue;
        }
        piece.cracks.push_back({start, end, plane->normal});
      }
    }

    ModelRenderer *renderer = ModelManager::GetInstance()->GetModelRenderer();
    piece.model = std::make_unique<Model>();
    piece.model->InitializeFromModelData(renderer, outer);
    if (!cap.vertices.empty()) {
      piece.capModel = std::make_unique<Model>();
      piece.capModel->InitializeFromModelData(renderer, cap);
    }

    pieces_.push_back(std::move(piece));
  }
}
