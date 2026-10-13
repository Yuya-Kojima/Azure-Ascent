#include "FracturePieceSet.h"
#include "Effect/MeshFracture.h"
#include "Math/MathUtil.h"
#include "Model/ModelManager.h"

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

  pieces_.reserve(meshes.size());
  for (Model::ModelData &mesh : meshes) {
    // 頂点 0 個ではバッファを作れない（VoronoiFracture は空を返さないが念のため）
    if (mesh.vertices.empty()) {
      continue;
    }

    Piece piece;
    // 原点を重心に移す（戻り値の重心が、破片を置く位置になる）
    piece.centroid = MeshFracture::CenterMesh(mesh);
    piece.direction = SafeNormalize(piece.centroid - sourceCentroid);

    piece.model = std::make_unique<Model>();
    piece.model->InitializeFromModelData(
        ModelManager::GetInstance()->GetModelRenderer(), mesh);

    pieces_.push_back(std::move(piece));
  }
}
