#include "MeshFracture.h"
#include "Math/MathUtil.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace MeshFracture {

namespace {

// 平面上とみなす距離の許容誤差
const float kOnPlaneEpsilon = 1e-5f;
// キャップの点を同一点とみなす距離（の2乗）
const float kMergeDistanceSq = 1e-8f;
// 体積がほぼ 0（潰れたメッシュ）とみなす値
// 体積は長さの3乗なので、距離用の kOnPlaneEpsilon とは別に持つ
const float kMinVolume = 1e-9f;

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

// 長さ 1 のランダムな向き（どの向きも同じ確率）
// 立方体 [-1,1]^3 の点をそのまま正規化すると角の方向に偏るので、
// 球の内側に入った点だけを採用してから正規化する（棄却法）
Vector3 RandomDirection(std::mt19937 &engine) {
  // 0 に近い点は正規化で誤差が大きくなるので捨てる
  const float kMinLengthSq = 1e-6f;
  std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
  while (true) {
    const Vector3 v = {dist(engine), dist(engine), dist(engine)};
    const float lengthSq = LengthSq(v);
    if (kMinLengthSq < lengthSq && lengthSq <= 1.0f) {
      return v * (1.0f / std::sqrt(lengthSq));
    }
  }
}

// 箱の表面のランダムな1点（衝撃点）
// 面は面積に比例した確率で選ぶ（細長い箱でも、表面のどこも同じ確率になる）
Vector3 RandomPointOnBoxSurface(const Vector3 &min, const Vector3 &max,
                                std::mt19937 &engine) {
  const Vector3 size = max - min;
  // 軸ごとの「その軸に垂直な面」の面積（例: X 軸に垂直な面は Y x Z）
  const float areas[3] = {size.y * size.z, size.x * size.z,
                          size.x * size.y};
  if (areas[0] + areas[1] + areas[2] <= 0.0f) {
    // 潰れた箱は表面を選べないので中心にする
    return (min + max) * 0.5f;
  }

  // まず箱の中の点を選び、選んだ軸だけ min か max の面に貼り付ける
  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
  Vector3 point = {min.x + size.x * dist01(engine),
                   min.y + size.y * dist01(engine),
                   min.z + size.z * dist01(engine)};
  std::discrete_distribution<int> pickAxis({areas[0], areas[1], areas[2]});
  const int axis = pickAxis(engine);
  const bool onMaxSide = dist01(engine) < 0.5f;
  float *components[3] = {&point.x, &point.y, &point.z};
  const float faceValue[2][3] = {{min.x, min.y, min.z}, {max.x, max.y, max.z}};
  *components[axis] = faceValue[onMaxSide ? 1 : 0][axis];
  return point;
}

// 箱からはみ出た値を、面で鏡のように折り返して中に戻す
// 単に面へ押し付けると種が面上に重なって薄い破片が増えるが、
// 折り返しなら密集の形を保てる
float ReflectIntoRange(float value, float min, float max) {
  if (value < min) {
    value = min + (min - value);
  } else if (value > max) {
    value = max - (value - max);
  }
  // 範囲より大きくはみ出した場合は、折り返しても外なので端に寄せる
  return std::clamp(value, min, max);
}

} // namespace

Plane MakePlane(const Vector3 &normal, const Vector3 &point) {
  Vector3 n = SafeNormalize(normal);
  return {n, -Dot(n, point)};
}

Plane FlipPlane(const Plane &plane) { return {-plane.normal, -plane.d}; }

Plane MakeBisectorPlane(const Vector3 &pi, const Vector3 &pj) {
  return MakePlane(pi - pj, (pi + pj) * 0.5f);
}

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

  bool hasFront = false;
  bool hasBack = false;

  for (const Model::VertexData &vertex : src.vertices) {
    float s = SignedDistance(plane, ToVector3(vertex.position));

    // 平面がメッシュに触れているだけ（切っていない）かを先に調べる
    // 平面上の頂点（|s| が小さい）は、表にも裏にも数えない
    // （数えると、平面と重なる面が両方の破片に残り、余分なキャップができる）
    if (s > kOnPlaneEpsilon) {
      hasFront = true;
    } else if (s <= -kOnPlaneEpsilon) {
      hasBack = true;
    }
  }

  // 表側に何もない → こちら側の破片はない
  if (!hasFront) {
    return {};
  }

  // 裏側に何もない → 何も切り落とさないので、元のまま
  if (!hasBack) {
    return src;
  }

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

std::vector<Model::ModelData>
VoronoiFracture(const Model::ModelData &src,
                const std::vector<Vector3> &seeds) {
  std::vector<Model::ModelData> pieces;

  // 外側: 誰の破片を作るか
  for (size_t i = 0; i < seeds.size(); ++i) {
    // 元のメッシュからスタートする
    Model::ModelData cell = src;

    // 内側: 誰との境目で切るか
    for (size_t j = 0; j < seeds.size(); ++j) {
      // 自分自身とは比べない
      if (i == j) {
        continue;
      }

      // 二等分面で切り、i 側だけ残す（cell を上書きする）
      cell = ClipMesh(cell, MakeBisectorPlane(seeds[i], seeds[j]));
    }

    // cell が空でなければ pieces に追加する
    if (!cell.vertices.empty()) {
      pieces.push_back(cell);
    }
  }
  return pieces;
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

Vector3 ComputeCentroid(const Model::ModelData &mesh) {
  float totalVolume = 0.0f;
  Vector3 weightedSum = {0.0f, 0.0f, 0.0f};

  for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    Vector3 a = ToVector3(mesh.vertices[mesh.indices[i + 0]].position);
    Vector3 b = ToVector3(mesh.vertices[mesh.indices[i + 1]].position);
    Vector3 c = ToVector3(mesh.vertices[mesh.indices[i + 2]].position);

    // 四面体（原点, a, b, c）の符号付き体積
    float volume = Dot(a, Cross(b, c)) / 6.0f;

    // 四面体の重心（4つの頂点の平均。原点 (0,0,0) も4つのうちの1つ）
    Vector3 tetraCenter = (a + b + c) * 0.25f;

    // 数直線の例の「長さの合計」と「中心 × 長さ の合計」にあたる
    totalVolume += volume;
    weightedSum += tetraCenter * volume;
  }

  // 体積がほぼ 0 のとき（潰れたメッシュ）は割り算できない
  if (std::abs(totalVolume) < kMinVolume) {
    if (mesh.vertices.empty()) {
      return {0.0f, 0.0f, 0.0f};
    }

    Vector3 average = {0.0f, 0.0f, 0.0f};
    for (const Model::VertexData &vertex : mesh.vertices) {
      average += ToVector3(vertex.position);
    }
    return average * (1.0f / mesh.vertices.size());
  }

  return weightedSum * (1.0f / totalVolume);
}

Vector3 CenterMesh(Model::ModelData &mesh) {
  const Vector3 centroid = ComputeCentroid(mesh);
  // 平行移動するだけなので、法線と UV はそのままでよい
  for (Model::VertexData &vertex : mesh.vertices) {
    vertex.position.x -= centroid.x;
    vertex.position.y -= centroid.y;
    vertex.position.z -= centroid.z;
  }
  return centroid;
}

void ComputeBounds(const Model::ModelData &mesh, Vector3 &outMin,
                   Vector3 &outMax) {
  if (mesh.vertices.empty()) {
    outMin = {0.0f, 0.0f, 0.0f};
    outMax = {0.0f, 0.0f, 0.0f};
    return;
  }
  outMin = ToVector3(mesh.vertices[0].position);
  outMax = outMin;
  for (const Model::VertexData &vertex : mesh.vertices) {
    Vector3 p = ToVector3(vertex.position);
    outMin = {(std::min)(outMin.x, p.x), (std::min)(outMin.y, p.y),
              (std::min)(outMin.z, p.z)};
    outMax = {(std::max)(outMax.x, p.x), (std::max)(outMax.y, p.y),
              (std::max)(outMax.z, p.z)};
  }
}

std::vector<Vector3> GenerateSeeds(const Vector3 &min, const Vector3 &max,
                                   uint32_t count, uint32_t randomSeed,
                                   const SeedBias &bias,
                                   Vector3 *outImpactPoint) {
  // std::mt19937 は同じシードなら同じ乱数列を返す（rand()
  // と違い、環境で結果が変わらない）
  std::mt19937 engine(randomSeed);
  std::uniform_real_distribution<float> distX(min.x, max.x);
  std::uniform_real_distribution<float> distY(min.y, max.y);
  std::uniform_real_distribution<float> distZ(min.z, max.z);

  std::vector<Vector3> seeds;
  seeds.reserve(count);

  if (!bias.enabled) {
    for (uint32_t i = 0; i < count; ++i) {
      seeds.push_back({distX(engine), distY(engine), distZ(engine)});
    }
    return seeds;
  }

  const Vector3 impactPoint = RandomPointOnBoxSurface(min, max, engine);
  if (outImpactPoint) {
    *outImpactPoint = impactPoint;
  }
  const float diagonal = Length(max - min);
  const float radius = bias.radius * diagonal;
  const float minSpacingSq =
      (bias.minSpacing * diagonal) * (bias.minSpacing * diagonal);

  // 0 以下だと乱数 0 のとき 0 の負の乗で無限大になるので、下限を設ける
  const float kMinFalloff = 0.01f;
  const float falloff = (std::max)(bias.falloff, kMinFalloff);

  // 近すぎる種を引き直す回数の上限
  // （狭い範囲に種を詰め込みすぎた場合に、無限ループしないため）
  const int kMaxAttempts = 16;

  std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
  for (uint32_t i = 0; i < count; ++i) {
    // くじ: 当たりなら衝撃点の近く、
    // はずれなら箱全体に均等に置く（大きな塊になる）
    const bool nearImpact = dist01(engine) < bias.ratio;

    Vector3 seed;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
      if (nearImpact) {
        // 乱数 u (0〜1) を falloff 乗すると 0 側に寄る
        // （u=0.5 なら 2乗で 0.25, 3乗で 0.125）
        // そのため衝撃点ほど種が密集し、離れるほどまばらになる
        // ＝破片が近くは細かく、遠くは大きくなる
        const float distance = radius * std::pow(dist01(engine), falloff);
        const Vector3 p = impactPoint + RandomDirection(engine) * distance;

        // 衝撃点は表面にあるので、半分ほどは箱の外に出る。折り返して中に戻す
        seed = {ReflectIntoRange(p.x, min.x, max.x),
                ReflectIntoRange(p.y, min.y, max.y),
                ReflectIntoRange(p.z, min.z, max.z)};
      } else {
        seed = {distX(engine), distY(engine), distZ(engine)};
      }

      // 既にある種と十分離れていれば採用する
      // （上限まで引き直したら、近くてもそのまま使う）
      const bool isFarEnough = std::all_of(
          seeds.begin(), seeds.end(), [&](const Vector3 &other) {
            return LengthSq(seed - other) >= minSpacingSq;
          });
      if (isFarEnough) {
        break;
      }
    }
    seeds.push_back(seed);
  }
  return seeds;
}

} // namespace MeshFracture
