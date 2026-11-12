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
  /// 部品の付く所の中の座標をワールド座標へ移す行列
  /// </summary>
  /// <param name="index">部品の番号</param>
  virtual const Matrix4x4 &GetPartFrame(int index) const = 0;

  /// <summary>
  /// 部品のモデルの向き（オイラー角）
  /// </summary>
  /// <param name="index">部品の番号</param>
  virtual Vector3 GetPartRotation(int index) const = 0;

  /// <summary>
  /// 撃つ役の部品が撃ってよいか
  /// </summary>
  virtual bool CanPartFire() const = 0;

  /// <summary>
  /// 部品が壊れた
  /// </summary>
  /// <param name="index">部品の番号</param>
  virtual void OnPartDestroyed(int index) = 0;

  /// <summary>
  /// 部品を今撃てるか（撃てない間はロックされず、弾も当たらない）
  /// </summary>
  /// <param name="index">部品の番号</param>
  virtual bool IsPartActive(int index) const {
    (void)index;
    return true;
  }
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
    Thruster, // 推進器
    Core      // 大蛇の光る節（溜めの間だけ撃てる）
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
  bool CanBeLockedOn() const override { return IsActive(); }
  bool CanDespawnBehindCamera() const override { return false; }

  /// <summary>
  /// 持ち主から切り離して静かに消す（倒されたことにはしない）
  /// </summary>
  void Vanish();

  /// <summary>
  /// 大きさを Setup で決めた大きさの k 倍にする
  /// </summary>
  void SetPulseScale(float k) { transform_.scale = baseScale_ * k; }

  Role GetRole() const { return role_; }
  const Vector3 &GetLocalPosition() const { return localPosition_; }

private:
  /// <summary>
  /// 持ち主から見て今撃てるか
  /// </summary>
  bool IsActive() const { return !owner_ || owner_->IsPartActive(index_); }

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
  Vector3 baseScale_ = {1.0f, 1.0f, 1.0f}; // Setup で決めた大きさ
  float fireTimer_ = 0.0f; // 撃ってよい間に進める時間（秒）
};
