#include "FractureDebris.h"
#include "Core/GameTime.h"
#include "Effect/BigEnemyDeath.h"
#include "Effect/FracturePieceSet.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/Object3dRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
// 角速度の乱数のシード（実行ごとの再現性のため固定。爆発ごとには違う値が続けて出る）
const uint32_t kRandomSeed = 1;

// 全爆発で共有する乱数（爆発のたびに作り直すと、毎回同じ回り方になってしまう）
std::mt19937 &GetRandomEngine() {
  static std::mt19937 engine(kRandomSeed);
  return engine;
}

// 回転と位置だけの行列の回転を、MakeRotateMatrix（X → Y → Z の順に回す）のオイラー角に直す
Vector3 ToEulerXYZ(const Matrix4x4 &m) {
  return {std::atan2(m.m[1][2], m.m[2][2]),
          std::asin(std::clamp(-m.m[0][2], -1.0f, 1.0f)),
          std::atan2(m.m[0][1], m.m[0][0])};
}

// 色に明るさの割合をかける（不透明度はそのまま）
Vector4 ScaleColor(const Vector4 &color, float rate) {
  return {color.x * rate, color.y * rate, color.z * rate, color.w};
}

// 2 つの色を t（0〜1）で混ぜる
Vector4 LerpColor(const Vector4 &a, const Vector4 &b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
          a.w + (b.w - a.w) * t};
}
} // namespace

FractureDebris::FractureDebris(Object3dRenderer *renderer,
                               const FracturePieceSet &pieceSet,
                               const Vector3 &position,
                               const Vector3 &rotation, const Vector3 &scale,
                               const Look &look,
                               const Vector3 &inheritedVelocity,
                               const Params &params)
    : pieceSet_(&pieceSet) {
  gravity_ = params.gravity;
  drag_ = params.drag;
  lifeTimer_ = params.lifetime;
  lifetime_ = params.lifetime;
  explosionSpeed_ = params.explosionSpeed;
  maxAngularSpeed_ = params.maxAngularSpeed;

  if (!renderer) {
    return;
  }
  CreatePieces(renderer, scale, look);

  // 敵の行列と同じ、拡大 → 回転 → 移動の順に置く（回り始めの向きは敵の回転と同じ）
  boxWorld_ = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, rotation, position);
  for (Piece &piece : pieces_) {
    piece.position = TransformPoint(piece.localCenter, boxWorld_);
    piece.rotation = rotation;
  }
  // 作った直後に外へ飛ばす（作った直後のフレームで原点に描かれないよう、置き場所も反映する）
  Release(inheritedVelocity);
}

FractureDebris::FractureDebris(Object3dRenderer *renderer,
                               const FracturePieceSet &pieceSet,
                               const Matrix4x4 &boxWorld, const Vector3 &scale,
                               const Look &look, const Params &params)
    : pieceSet_(&pieceSet) {
  gravity_ = params.gravity;
  drag_ = params.drag;
  lifeTimer_ = params.lifetime;
  lifetime_ = params.lifetime;
  explosionSpeed_ = params.explosionSpeed;
  maxAngularSpeed_ = params.maxAngularSpeed;
  isHeld_ = true;

  if (!renderer) {
    return;
  }
  CreatePieces(renderer, scale, look);
  // 割れ目はまだ開いていない
  Hold(boxWorld, Opening{});
}

FractureDebris::~FractureDebris() = default;

void FractureDebris::CreatePieces(Object3dRenderer *renderer, const Vector3 &scale,
                                  const Look &look) {
  scale_ = scale;
  capGlowColor_ = look.capGlowColor;

  const std::vector<FracturePieceSet::Piece> &shapes = pieceSet_->GetPieces();
  pieces_.reserve(shapes.size());
  for (const FracturePieceSet::Piece &shape : shapes) {
    Piece piece;

    // 形は形セットのものを借りる（Model は共有、Object3d だけ自分で持つ）
    piece.object = std::make_unique<Object3d>();
    piece.object->Initialize(renderer);
    piece.object->SetModel(shape.model.get());
    piece.object->SetScale(scale);
    piece.object->SetColor(look.color);
    piece.object->SetRimLight(look.rimColor, look.rimIntensity, look.rimPower);

    // 割れ口の面は、光らせる色があればライティングなしでその色のまま、なければ外側と同じ見た目
    if (shape.capModel) {
      piece.capObject = std::make_unique<Object3d>();
      piece.capObject->Initialize(renderer);
      piece.capObject->SetModel(shape.capModel.get());
      piece.capObject->SetScale(scale);
      if (capGlowColor_) {
        piece.capObject->SetColor(*capGlowColor_);
        piece.capObject->SetEnableLighting(false);
        piece.capObject->SetFlatUnlit(true);
      } else {
        piece.capObject->SetColor(look.color);
        piece.capObject->SetRimLight(look.rimColor, look.rimIntensity, look.rimPower);
      }
    }

    // 重心は元のモデルの大きさでの位置なので、箱の大きさをかけて箱の中の位置にする
    piece.localCenter = {shape.centroid.x * scale.x, shape.centroid.y * scale.y,
                         shape.centroid.z * scale.z};
    piece.localOutward = shape.direction;
    piece.capShape = shape.capModel ? &shape.capModel->GetModelData() : nullptr;
    pieces_.push_back(std::move(piece));
  }
}

void FractureDebris::Update() {
  if (isHeld_) {
    return;
  }

  // 寿命を減らしてから判定する（先に判定すると1フレーム余計に生きる）
  // 消えると決めたフレームでは、もう動かさない
  lifeTimer_ -= kFixedDeltaTime;
  if (lifeTimer_ <= 0.0f) {
    Destroy();
    return;
  }
  releasedTime_ += kFixedDeltaTime;

  for (Piece &piece : pieces_) {
    // 半陰的オイラー法: 速度を先に更新し、新しい速度で位置を動かす
    // どれも「1秒あたりの量」なので、1フレームの時間を掛けてから足す
    piece.velocity += gravity_ * kFixedDeltaTime;

    // 空気抵抗で速度を減らす
    piece.velocity = piece.velocity * (1.0f - drag_ * kFixedDeltaTime);
    piece.position += piece.velocity * kFixedDeltaTime;
    piece.rotation += piece.angularVelocity * kFixedDeltaTime;

    ApplyTransform(piece);
    if (burning_) {
      UpdateBurning(piece);
    }
  }
}

void FractureDebris::Draw3D() {
  for (Piece &piece : pieces_) {
    piece.object->Draw();
    if (piece.capObject) {
      piece.capObject->Draw();
    }
  }
}

void FractureDebris::Hold(const Matrix4x4 &boxWorld, const Opening &opening) {
  if (!isHeld_) {
    return;
  }
  boxWorld_ = boxWorld;
  openGap_ = opening.gap;
  const Vector3 rotation = ToEulerXYZ(boxWorld);
  for (Piece &piece : pieces_) {
    // 前線が重心を過ぎた距離に合わせて開く
    const float distance =
        Length(TransformPoint(piece.localCenter, boxWorld) - opening.frontCenter);
    piece.openRate = std::clamp((opening.frontRadius - distance) /
                                    (std::max)(opening.openWidth, 1e-3f),
                                0.0f, 1.0f);
    piece.position = TransformPoint(
        piece.localCenter + piece.localOutward * (opening.gap * piece.openRate), boxWorld);
    piece.rotation = rotation;
    if (piece.capObject && capGlowColor_) {
      piece.capObject->SetColor(ScaleColor(*capGlowColor_, piece.openRate));
    }
    ApplyTransform(piece);
  }
}

void FractureDebris::Release(const Vector3 &inheritedVelocity) {
  isHeld_ = false;
  releasedTime_ = 0.0f;
  std::uniform_real_distribution<float> angularDist(-maxAngularSpeed_, maxAngularSpeed_);
  for (Piece &piece : pieces_) {
    // 外向きに飛ばす速度に、引き継ぐ速度を足す
    // （ゲームではカメラが進み続けるので、引き継がないと破片が後ろへ流れて見える）
    piece.velocity =
        TransformNormal(piece.localOutward, boxWorld_) * explosionSpeed_ + inheritedVelocity;

    // 角速度は各軸ランダム（破片ごとに違う回り方にする）
    std::mt19937 &engine = GetRandomEngine();
    piece.angularVelocity = {angularDist(engine), angularDist(engine), angularDist(engine)};

    // 割れ口は開ききった明るさにする
    piece.openRate = 1.0f;
    if (piece.capObject && capGlowColor_) {
      piece.capObject->SetColor(*capGlowColor_);
    }
    ApplyTransform(piece);
  }
}

bool FractureDebris::PickOpenCrackPoint(std::mt19937 &random, Vector3 &point,
                                        Vector3 &normal) const {
  if (!pieceSet_) {
    return false;
  }
  const std::vector<FracturePieceSet::Piece> &shapes = pieceSet_->GetPieces();

  // 割れ目が開いていて、割れ目の線を持つ破片を数え、その中から 1 つ選ぶ
  int openCount = 0;
  for (size_t i = 0; i < pieces_.size(); ++i) {
    if (pieces_[i].openRate > 0.0f && !shapes[i].cracks.empty()) {
      ++openCount;
    }
  }
  if (openCount == 0) {
    return false;
  }
  int pick = std::uniform_int_distribution<int>(0, openCount - 1)(random);
  size_t index = 0;
  for (; index < pieces_.size(); ++index) {
    if (pieces_[index].openRate > 0.0f && !shapes[index].cracks.empty() && pick-- == 0) {
      break;
    }
  }
  const Piece &piece = pieces_[index];
  const std::vector<FracturePieceSet::CrackSegment> &cracks = shapes[index].cracks;

  // 線を選び、線の上の点を選ぶ（形セットの座標 → 箱の大きさをかけ、破片と一緒に開いた分ずらす → ワールド）
  const FracturePieceSet::CrackSegment &crack =
      cracks[std::uniform_int_distribution<size_t>(0, cracks.size() - 1)(random)];
  const float t = std::uniform_real_distribution<float>(0.0f, 1.0f)(random);
  const Vector3 onLine = crack.start + (crack.end - crack.start) * t;
  const Vector3 local = Vector3{onLine.x * scale_.x, onLine.y * scale_.y, onLine.z * scale_.z} +
                        piece.localOutward * (openGap_ * piece.openRate);
  point = TransformPoint(local, boxWorld_);

  // 面の外向きは、箱の大きさで割ってから回す（拡大のかかった面の向きの直し方）
  const Vector3 scaledNormal = {crack.normal.x / scale_.x, crack.normal.y / scale_.y,
                                crack.normal.z / scale_.z};
  normal = SafeNormalize(TransformNormal(SafeNormalize(scaledNormal), boxWorld_));
  return true;
}

void FractureDebris::UpdateBurning(Piece &piece) {
  const Burning &burning = *burning_;

  // 割れ口を、放したときの光る色から冷めた色へ近づける
  if (piece.capObject && capGlowColor_) {
    const float cool =
        std::clamp(releasedTime_ / (std::max)(burning.coolSeconds, 1e-3f), 0.0f, 1.0f);
    piece.capObject->SetColor(LerpColor(*capGlowColor_, burning.cooledColor, cool));
  }

  // 炎と黒煙は燃え始めが一番多く、寿命の終わりへ 0 に減る
  const float remain =
      std::clamp(1.0f - releasedTime_ / (std::max)(lifetime_, 1e-3f), 0.0f, 1.0f);
  const Vector3 trailVelocity = piece.velocity * burning.trailInherit;
  piece.flameCarry += burning.flameRate * remain * kFixedDeltaTime;
  piece.smokeCarry += burning.smokeRate * remain * kFixedDeltaTime;
  for (; piece.flameCarry >= 1.0f; piece.flameCarry -= 1.0f) {
    BigEnemyDeath::EmitFlame(PickCapPoint(piece), trailVelocity, 1);
  }
  for (; piece.smokeCarry >= 1.0f; piece.smokeCarry -= 1.0f) {
    BigEnemyDeath::EmitTrailSmoke(PickCapPoint(piece), trailVelocity, 1);
  }
}

Vector3 FractureDebris::PickCapPoint(const Piece &piece) const {
  if (!piece.capShape || piece.capShape->indices.size() < 3) {
    return piece.position;
  }
  // 割れ口の三角形を 1 つ選び、三角形の中の点を選ぶ（破片の重心からの位置、元のモデルの大きさ）
  std::mt19937 &engine = GetRandomEngine();
  const size_t triangleCount = piece.capShape->indices.size() / 3;
  const size_t triangle = std::uniform_int_distribution<size_t>(0, triangleCount - 1)(engine);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  float u = unit(engine);
  float v = unit(engine);
  if (u + v > 1.0f) {
    u = 1.0f - u;
    v = 1.0f - v;
  }
  const Vector4 &a = piece.capShape->vertices[piece.capShape->indices[triangle * 3 + 0]].position;
  const Vector4 &b = piece.capShape->vertices[piece.capShape->indices[triangle * 3 + 1]].position;
  const Vector4 &c = piece.capShape->vertices[piece.capShape->indices[triangle * 3 + 2]].position;
  const Vector3 onCap = {a.x + (b.x - a.x) * u + (c.x - a.x) * v,
                         a.y + (b.y - a.y) * u + (c.y - a.y) * v,
                         a.z + (b.z - a.z) * u + (c.z - a.z) * v};
  // 破片の Object3d と同じ、拡大 → 回転 → 移動の順にワールドへ直す
  return TransformPoint(onCap, MakeAffineMatrix(scale_, piece.rotation, piece.position));
}

void FractureDebris::ApplyTransform(Piece &piece) {
  piece.object->SetTranslation(piece.position);
  piece.object->SetRotation(piece.rotation);
  piece.object->Update();
  if (piece.capObject) {
    piece.capObject->SetTranslation(piece.position);
    piece.capObject->SetRotation(piece.rotation);
    piece.capObject->Update();
  }
}
