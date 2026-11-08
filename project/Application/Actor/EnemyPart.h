#pragma once
#include "Actor/Enemy.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"

/// <summary>
/// 部品を持つ大きな敵。部品の位置を決める行列を返し、部品が壊れたことを受け取る
/// </summary>
class IEnemyPartOwner {
public:
  virtual ~IEnemyPartOwner() = default;

  /// <summary>
  /// 持ち主の中の座標をワールド座標へ移す行列
  /// </summary>
  virtual const Matrix4x4 &GetPartFrame() const = 0;

  /// <summary>
  /// 部品のモデルの向き（オイラー角）
  /// </summary>
  virtual Vector3 GetPartRotation() const = 0;

  /// <summary>
  /// 撃つ役の部品が撃ってよいか
  /// </summary>
  virtual bool CanPartFire() const = 0;

  /// <summary>
  /// 部品が壊れた
  /// </summary>
  /// <param name="index">部品の番号</param>
  virtual void OnPartDestroyed(int index) = 0;
};

/// <summary>
/// 持ち主に付いて動く部品の敵。持ち主の行列 × 自分の位置で毎フレーム置く
/// 画面の外やカメラの後ろでも消えず、持ち主と一緒に消える
/// </summary>
class EnemyPart : public Enemy {
public:
  // 部品の役
  enum class Role {
    Balloon,  // 気嚢
    Turret,   // 砲座（自機を撃つ）
    Thruster  // 推進器
  };

  EnemyPart();
  ~EnemyPart() override;

  /// <summary>
  /// 持ち主・番号・役・持ち主の中の位置・半分の大きさを決める（Initialize のあとに呼ぶ）
  /// </summary>
  void Setup(IEnemyPartOwner *owner, int index, Role role,
             const Vector3 &localPosition, const Vector3 &halfSize);

  void Update() override;
  void TakeDamage(int damage, bool isSelfDestruct = false) override;
  bool CanDespawnBehindCamera() const override { return false; }

  /// <summary>
  /// 持ち主から切り離して静かに消す（倒されたことにはしない）
  /// </summary>
  void Vanish();

  Role GetRole() const { return role_; }
  const Vector3 &GetLocalPosition() const { return localPosition_; }

private:
  /// <summary>
  /// 砲座の役なら、撃ってよい間に自機の先回りの位置へ弾を撃つ
  /// </summary>
  void UpdateFire();

  static constexpr float kFrameTime_ = 1.0f / 60.0f;       // 1 フレームの秒数
  static constexpr float kFireInterval_ = 2.0f;            // 撃つ間隔（秒）
  static constexpr float kFirstShotDelay_ = 1.0f;          // 撃ってよくなってから初めの 1 発までの秒数
  static constexpr float kMaxShotDistance_ = 280.0f;       // 撃つのは自機からこの距離より近いときだけ（m）
  static constexpr float kMinFairDistance_ = 60.0f;        // 撃つのは自機よりこの距離以上前にいるときだけ（m）

  IEnemyPartOwner *owner_ = nullptr;
  int index_ = 0;
  Role role_ = Role::Balloon;
  Vector3 localPosition_ = {0.0f, 0.0f, 0.0f};
  float fireTimer_ = 0.0f; // 撃ってよい間に進める時間（秒）
};
