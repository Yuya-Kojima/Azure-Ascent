#include "FractureDebris.h"
#include "Core/GameTime.h"
#include "Effect/FracturePieceSet.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/Object3dRenderer.h"
#include <cstdint>
#include <random>

namespace {
// 角速度の乱数のシード（実行ごとの再現性のため固定。爆発ごとには違う値が続けて出る）
const uint32_t kRandomSeed = 1;

// 全爆発で共有する乱数（爆発のたびに作り直すと、毎回同じ回り方になってしまう）
std::mt19937 &GetRandomEngine() {
  static std::mt19937 engine(kRandomSeed);
  return engine;
}
} // namespace

FractureDebris::FractureDebris(Object3dRenderer *renderer,
                               const FracturePieceSet &pieceSet,
                               const Vector3 &position, const Vector3 &scale,
                               const Vector4 &color,
                               const Vector3 &inheritedVelocity,
                               const Params &params) {
  gravity_ = params.gravity;
  drag_ = params.drag;
  lifeTimer_ = params.lifetime;

  if (!renderer) {
    return;
  }

  std::uniform_real_distribution<float> angularDist(-params.maxAngularSpeed,
                                                    params.maxAngularSpeed);

  const std::vector<FracturePieceSet::Piece> &shapes = pieceSet.GetPieces();
  pieces_.reserve(shapes.size());
  for (const FracturePieceSet::Piece &shape : shapes) {
    Piece piece;

    // 形は形セットのものを借りる（Model は共有、Object3d だけ自分で持つ）
    piece.object = std::make_unique<Object3d>();
    piece.object->Initialize(renderer);
    piece.object->SetModel(shape.model.get());
    piece.object->SetScale(scale);
    piece.object->SetColor(color);

    // 重心は元のモデルの大きさでの位置なので、敵の大きさを掛けてから置く
    piece.position = position + Vector3{shape.centroid.x * scale.x,
                                        shape.centroid.y * scale.y,
                                        shape.centroid.z * scale.z};

    // 外向きに飛ばす速度に、引き継ぐ速度を足す
    // （ゲームではカメラが進み続けるので、引き継がないと破片が後ろへ流れて見える）
    piece.velocity =
        shape.direction * params.explosionSpeed + inheritedVelocity;

    // 角速度は各軸ランダム（破片ごとに違う回り方にする）
    std::mt19937 &engine = GetRandomEngine();
    piece.angularVelocity = {angularDist(engine), angularDist(engine),
                             angularDist(engine)};

    // 作った直後のフレームで原点に描かれないよう、すぐに位置を反映する
    ApplyTransform(piece);
    pieces_.push_back(std::move(piece));
  }
}

FractureDebris::~FractureDebris() = default;

void FractureDebris::Update() {

  // 寿命を減らしてから判定する（先に判定すると1フレーム余計に生きる）
  // 消えると決めたフレームでは、もう動かさない
  lifeTimer_ -= kFixedDeltaTime;
  if (lifeTimer_ <= 0.0f) {
    Destroy();
    return;
  }

  for (Piece &piece : pieces_) {
    // 半陰的オイラー法: 速度を先に更新し、新しい速度で位置を動かす
    // どれも「1秒あたりの量」なので、1フレームの時間を掛けてから足す
    piece.velocity += gravity_ * kFixedDeltaTime;

    // 空気抵抗で速度を減らす
    piece.velocity = piece.velocity * (1.0f - drag_ * kFixedDeltaTime);
    piece.position += piece.velocity * kFixedDeltaTime;
    piece.rotation += piece.angularVelocity * kFixedDeltaTime;

    ApplyTransform(piece);
  }
}

void FractureDebris::Draw3D() {
  for (Piece &piece : pieces_) {
    piece.object->Draw();
  }
}

void FractureDebris::ApplyTransform(Piece &piece) {
  piece.object->SetTranslation(piece.position);
  piece.object->SetRotation(piece.rotation);
  piece.object->Update();
}
