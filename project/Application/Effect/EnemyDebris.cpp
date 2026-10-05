#include "EnemyDebris.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/Object3dRenderer.h"
#include <algorithm>
#include <cstdlib>
#include <cmath>

EnemyDebris::EnemyDebris(Object3dRenderer *renderer, const Vector3 &centerPos,
                         const Vector3 &enemyScale, const Vector4 &color) {
  if (!renderer) {
    return;
  }

  // 6個の破片ブロックを生成
  const int kPieceCount = 6;
  pieces_.reserve(kPieceCount);

  // 破片の基本サイズ（敵のスケールの約35%）
  Vector3 pieceBaseScale = {
      enemyScale.x * 0.35f,
      enemyScale.y * 0.35f,
      enemyScale.z * 0.35f
  };

  for (int i = 0; i < kPieceCount; ++i) {
    Piece piece;
    piece.model = std::make_unique<Object3d>();
    piece.model->Initialize(renderer);
    piece.model->SetModel("__builtin_box"); // 汎用ボックスメッシュ

    // 敵のカラーをベースに、破片ごとにわずかな明度差をつけて立体感を出す
    float brightness = 0.85f + (static_cast<float>(std::rand()) / RAND_MAX) * 0.4f; // 0.85 〜 1.25
    Vector4 pieceColor = {
        (std::min)(color.x * brightness, 1.0f),
        (std::min)(color.y * brightness, 1.0f),
        (std::min)(color.z * brightness, 1.0f),
        1.0f
    };
    piece.model->SetColor(pieceColor);

    // 少しバラつかせた破片サイズ
    float scaleJitter = 0.7f + (static_cast<float>(std::rand()) / RAND_MAX) * 0.6f;
    piece.baseScale = {
        pieceBaseScale.x * scaleJitter,
        pieceBaseScale.y * scaleJitter,
        pieceBaseScale.z * scaleJitter
    };
    piece.position = centerPos;

    // 放射状のランダム初速（爆発的に四方に弾け飛ぶ）
    float vx = (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f);
    float vy = (static_cast<float>(std::rand()) / RAND_MAX * 1.5f - 0.2f); // やや上向き
    float vz = (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f);
    float speed = 12.0f + (static_cast<float>(std::rand()) / RAND_MAX) * 16.0f; // 12 〜 28 m/s
    piece.velocity = {vx * speed, vy * speed, vz * speed};

    // ランダムな回転角と回転速度（激しくタンブリング）
    piece.rotation = {
        static_cast<float>(std::rand()) / RAND_MAX * 3.1415f * 2.0f,
        static_cast<float>(std::rand()) / RAND_MAX * 3.1415f * 2.0f,
        static_cast<float>(std::rand()) / RAND_MAX * 3.1415f * 2.0f
    };
    piece.angularVelocity = {
        (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * 12.0f,
        (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * 12.0f,
        (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f) * 12.0f
    };

    // 生成直後の初フレームでワールド原点(0,0,0)に描画されないよう、即座に敵の位置で初期行列を確定
    piece.model->SetTranslation(piece.position);
    piece.model->SetRotation(piece.rotation);
    piece.model->SetScale(piece.baseScale);
    piece.model->Update();

    pieces_.push_back(std::move(piece));
  }
}

void EnemyDebris::Initialize() {
  // 初期化時は特になし
}

void EnemyDebris::Update() {
  const float dt = 1.0f / 60.0f;
  lifeTimer_ -= dt;
  if (lifeTimer_ <= 0.0f) {
    Destroy();
    return;
  }

  // 終盤（残り0.15秒以下）で縮小フェードアウト
  float scaleFactor = 1.0f;
  if (lifeTimer_ < 0.15f) {
    scaleFactor = lifeTimer_ / 0.15f;
  }

  const float gravity = -35.0f; // 重力加速度

  for (auto &piece : pieces_) {
    // 物理移動（初速＋空気抵抗＋重力）
    piece.velocity.y += gravity * dt;
    piece.velocity.x *= 0.96f;
    piece.velocity.z *= 0.96f;

    piece.position.x += piece.velocity.x * dt;
    piece.position.y += piece.velocity.y * dt;
    piece.position.z += piece.velocity.z * dt;

    // 回転
    piece.rotation.x += piece.angularVelocity.x * dt;
    piece.rotation.y += piece.angularVelocity.y * dt;
    piece.rotation.z += piece.angularVelocity.z * dt;

    if (piece.model) {
      piece.model->SetTranslation(piece.position);
      piece.model->SetRotation(piece.rotation);
      piece.model->SetScale({
          piece.baseScale.x * scaleFactor,
          piece.baseScale.y * scaleFactor,
          piece.baseScale.z * scaleFactor
      });
      piece.model->Update();
    }
  }
}

void EnemyDebris::Draw3D() {
  for (auto &piece : pieces_) {
    if (piece.model) {
      piece.model->Draw();
    }
  }
}
