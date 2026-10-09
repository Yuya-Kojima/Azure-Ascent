#pragma once
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <memory>
#include <vector>

class Object3d;
class Object3dRenderer;
class FracturePieceSet;

/// <summary>
/// メッシュ破砕の撃破演出（1回の爆発）
/// 事前に割った破片の形（FracturePieceSet）を借りて置き、物理で飛ばす
/// 寿命が来たら Destroy() し、ActorManager に消してもらう（EnemyDebris と同じ形）
/// </summary>
class FractureDebris : public BaseActor {

public: // 型
  // 調整値
  struct Params {
    float explosionSpeed = 3.0f;             // 外向きの速さ（1秒あたりの距離）
    Vector3 gravity = {0.0f, -9.8f, 0.0f};   // 重力加速度
    float maxAngularSpeed = 6.0f;            // 角速度の最大値（ラジアン/秒）
    float lifetime = 1.5f;                   // 寿命（秒）
    // 空気抵抗（1秒あたりに速度が減る割合）
    // 2.4 で旧演出 EnemyDebris の「毎フレーム速度を 96% にする」とほぼ同じ減り方になる（(1 - 0.96) x 60）
    float drag = 2.4f;
  };

public: // メンバ関数
  /// <summary>
  /// 破片を置いて、爆発させる
  /// </summary>
  /// <param name="renderer">破片の Object3d を描くレンダラー</param>
  /// <param name="pieceSet">破片の形（借りるだけ。この爆発より長生きすること）</param>
  /// <param name="position">爆発の中心（敵の位置）</param>
  /// <param name="scale">敵の大きさ（形セットは元のモデルの大きさで作ってある）</param>
  /// <param name="color">破片の色（敵の色を引き継ぐ）</param>
  /// <param name="inheritedVelocity">引き継ぐ速度（レールの進む速度）</param>
  /// <param name="params">調整値</param>
  FractureDebris(Object3dRenderer *renderer, const FracturePieceSet &pieceSet,
                 const Vector3 &position, const Vector3 &scale,
                 const Vector4 &color, const Vector3 &inheritedVelocity,
                 const Params &params);
  // unique_ptr<Object3d> を前方宣言のまま持つため、デストラクタは .cpp で定義する
  ~FractureDebris() override;

  /// <summary>
  /// 更新（寿命と物理）
  /// </summary>
  void Update() override;

  /// <summary>
  /// 描画
  /// </summary>
  void Draw3D() override;

private: // 型
  // 破片1つ分（形は借り物、ここは置き場所と動きだけを持つ）
  struct Piece {
    std::unique_ptr<Object3d> object = nullptr;
    Vector3 position = {0.0f, 0.0f, 0.0f};        // 今の位置
    Vector3 velocity = {0.0f, 0.0f, 0.0f};        // 1秒あたりに進む量
    Vector3 rotation = {0.0f, 0.0f, 0.0f};        // 今の回転角（ラジアン）
    Vector3 angularVelocity = {0.0f, 0.0f, 0.0f}; // 1秒あたりに回る量（ラジアン）
  };

private: // メンバ関数
  /// <summary>
  /// 破片の位置と回転を Object3d に反映する
  /// </summary>
  void ApplyTransform(Piece &piece);

private: // メンバ変数
  // 破片
  std::vector<Piece> pieces_;

  // 重力加速度
  Vector3 gravity_ = {0.0f, 0.0f, 0.0f};

  // 空気抵抗（1秒あたりに速度が減る割合）
  float drag_ = 0.0f;

  // 残り寿命（秒）
  float lifeTimer_ = 0.0f;
};
