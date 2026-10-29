#pragma once
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <functional>
#include <memory>

#include "Render/Object3d/Object3d.h"
class SphereCollider;
class ICamera;
class IParticleEmitter;
class ParticleEmitter;
class Player;
#include "Behavior/IEnemyBehavior.h"
#include "Score/VolleyId.h"

enum class MoveType {
  Straight,
  Parallel,
  SineWave,
  Stationary,
  Fighter,
  Meteor,
  Strafe,
  Turret
};

class Enemy : public BaseActor {
public:
  Enemy();
  ~Enemy() override;

  void Initialize() override;
  void Update() override;
  void UpdateTransform() override;
  void Draw3D() override;
  void OnCollision(class Collider *other) override;

  // ロックオン対象になるかどうか
  virtual bool IsLockOnTarget() const {
    if (behavior_) {
      return behavior_->IsLockOnTarget();
    }
    return true;
  }

  // ボスかどうかの判定（デスポーン除外等で使用）
  virtual bool IsBoss() const { return false; }

  // 画面内に映っているかどうかの判定（マージン付き）
  bool IsInScreen(float margin = 0.0f) const;

  // 表示用の3Dモデルを外から渡してセットする
  virtual void SetModel(std::unique_ptr<Object3d> model) {
    model_ = std::move(model);
  }
  Object3d *GetModel() const { return model_.get(); }
  void SetBaseColor(const Vector4 &color) { baseColor_ = color; }
  const Vector4 &GetBaseColor() const { return baseColor_; }

  SphereCollider *GetCollider() const { return collider_.get(); }

  // 移動方向・軌道のセッター/ゲッター
  void SetMoveDirection(const Vector3 &dir) { moveDirection_ = dir; }
  const Vector3 &GetMoveDirection() const { return moveDirection_; }
  void SetMoveType(MoveType type) { moveType_ = type; }
  MoveType GetMoveType() const { return moveType_; }
  void SetBehavior(std::unique_ptr<IEnemyBehavior> behavior) {
    behavior_ = std::move(behavior);
  }

  void SetCamera(const ICamera *camera) { camera_ = camera; }
  const ICamera *GetCamera() const { return camera_; }

  // ベースとなるカメラベクトル（RailCamera等の揺れを無視した純粋な空間軸）
  const Vector3 &GetBasePosition() const { return basePos_; }
  const Vector3 &GetBaseForward() const { return baseForward_; }
  const Vector3 &GetBaseRight() const { return baseRight_; }
  const Vector3 &GetBaseUp() const { return baseUp_; }
  void SetSpawnOffset(const Vector3 &offset) { spawnOffset_ = offset; }
  const Vector3 &GetSpawnOffset() const { return spawnOffset_; }
  float GetAliveTime() const { return aliveTime_; }
  void SetPlayer(const Player *player) { player_ = player; }
  const Player *GetPlayer() const { return player_; }

  // Transformへのアクセス（BaseActorのprotectedメンバ）
  Transform &GetTransform() { return transform_; }

  // ステータスのゲッター/セッター
  int GetHP() const { return hp_; }
  void SetHP(int hp) { hp_ = hp; }
  float GetSpeed() const { return speed_; }
  void SetSpeed(float speed) { speed_ = speed; }

  // ダメージを受ける処理
  virtual void TakeDamage(int damage, bool isSelfDestruct = false);

  // 死亡時のコールバック設定（boolは自爆かどうか）
  void SetOnDestroyedCallback(std::function<void(bool)> cb) {
    onDestroyedCallback_ = cb;
  }

  std::function<void(bool)> GetOnDestroyedCallback() const {
    return onDestroyedCallback_;
  }

  // 最後に当たった自機の弾の射撃番号（ロックオンの一斉射撃でなければ kNoVolleyId）
  void SetLastHitVolley(int volleyId) { lastHitVolley_ = volleyId; }
  int GetLastHitVolley() const { return lastHitVolley_; }

protected:
  std::unique_ptr<Object3d> model_;
  std::unique_ptr<SphereCollider> collider_;

  // 死亡フラグ
  bool isDead_ = false; // 体力
  int hp_ = 3;          // 体力
  float speed_ = 0.5f;  // 移動速度
  Vector3 moveDirection_ = {
      0.0f, 0.0f,
      -1.0f}; // 進行方向ベクトル（デフォルトはワールドZマイナス方向）
  MoveType moveType_ = MoveType::Straight;
  std::unique_ptr<IEnemyBehavior> behavior_;
  const ICamera *camera_ = nullptr;
  const Player *player_ = nullptr;
  Vector3 spawnOffset_ = {0.0f, 0.0f, 0.0f};
  float aliveTime_ = 0.0f;

  // 直前の1フレームで動いた量（撃破時に、破片へ勢いを引き継ぐために保存しておく）
  // CalculateVelocityForCollision は呼ぶたびに前の位置を上書きするので、撃破時に呼び直すと 0 になる
  Vector3 frameMovement_ = {0.0f, 0.0f, 0.0f};

  // 毎フレーム更新される基準座標群
  Vector3 basePos_ = {0.0f, 0.0f, 0.0f};
  Vector3 baseForward_ = {0.0f, 0.0f, 1.0f};
  Vector3 baseRight_ = {1.0f, 0.0f, 0.0f};
  Vector3 baseUp_ = {0.0f, 1.0f, 0.0f};

  // --- 演出用パラメータ ---
  int hitFlashTimer_ = 0;                        // 被弾時の点滅タイマー
  Vector4 baseColor_ = {1.0f, 1.0f, 1.0f, 1.0f}; // 基本色

  std::function<void(bool)> onDestroyedCallback_ = nullptr;

  int lastHitVolley_ = kNoVolleyId;
};
