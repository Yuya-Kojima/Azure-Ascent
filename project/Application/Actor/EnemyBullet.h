#pragma once
#include "Actor/IClearedByBoss.h"
#include "Actor/ILockOnHittable.h"
#include "Actor/IPlayerBulletHittable.h"
#include "Actor/IPlayerContact.h"
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include <memory>
#include <vector>

class Player;
class Object3d;
class Object3dRenderer;
class SphereCollider;

enum class EnemyBulletType {
  NormalDestructible, // 通常ショットで破壊可能
  LockOnDestructible, // ロックオン可能
  Indestructible      // 破壊不可
};

class EnemyBullet : public BaseActor,
                    public ILockOnHittable,
                    public IPlayerBulletHittable,
                    public IPlayerContact,
                    public IClearedByBoss {
public:
  // 砲台・砦の砲座が自機を狙って撃つ弾の速さ（m/フレーム）
  static constexpr float kAimedShotSpeed_ = 1.3f;

  EnemyBullet();
  ~EnemyBullet() override;

  void Initialize(Object3dRenderer* renderer, const Vector3& startPos, const Vector3& velocity, const Player* player, EnemyBulletType type = EnemyBulletType::NormalDestructible);

  /// <summary>
  /// 入れ物から弾を 1 発取り出して撃つ（ActorManager に登録する）
  /// </summary>
  /// <param name="swarmWaitFrames">誘導弾が展開してから撃ち出されるまでのフレーム数（誘導弾のときだけ使う）</param>
  static void Spawn(const Vector3& startPos, const Vector3& velocity, const Player* player,
                    EnemyBulletType type = EnemyBulletType::NormalDestructible, int swarmWaitFrames = 0);
  
  void Update() override;
  void Draw3D() override;
  void Explode(); // 破壊エフェクトを伴って消滅する

  // 今ぶつかったら自機にダメージを与えるか（壊れた後は与えない）
  bool CanHitPlayer() const override { return !isDead_; }
  // 自機に当たった。弾は消える
  void OnHitPlayer() override { isDead_ = true; }

  // ロックオンできるのは、ロックオン可能弾（ボスのミサイルなど）だけ
  bool CanBeLockedOn() const override { return type_ == EnemyBulletType::LockOnDestructible; }
  // 自機の弾が当たった: ダメージを受け、HP が尽きたら壊れる（破壊不可弾は壊れない）
  void OnPlayerBulletHit(int damage, int volleyId) override;

  // ボスの形態変化が始まった: その場で消える
  void OnBossTransition() override { isDead_ = true; }
  // ボスが撃破された: 誘爆して消える
  void OnBossDefeated() override { Explode(); }

  EnemyBulletType GetBulletType() const { return type_; }
  void SetBulletType(EnemyBulletType type) { type_ = type; }
  void SetSwarmWait(int frames) { swarmWaitFrames_ = frames; }

private:
  // 入れ物へしまう前に、当たり判定を外す
  void OnReturnToPool() override;

  std::unique_ptr<Object3d> object3d_;
  std::unique_ptr<Object3d> crystalObject_; // 自転用子オブジェクト（進行軸と自転軸の分離）
  std::unique_ptr<SphereCollider> collider_;
  Vector3 velocity_ = {0.0f, 0.0f, 0.0f};
  float rollAngle_ = 0.0f; // 自転角（Roll軸）
  int lifeTimer_ = 0;
  int hp_ = 1;
  Object3dRenderer* renderer_ = nullptr;
  const Player* player_ = nullptr;
  EnemyBulletType type_ = EnemyBulletType::NormalDestructible;

  // 誘導弾用パラメータ
  float homingStrength_ = 0.0f;
  int aliveFrames_ = 0;
  int swarmWaitFrames_ = 0;

  // トレイル（リボン軌跡）用パラメータ
  std::vector<Vector3> trailHistory_;
  static constexpr size_t kMaxTrailPoints = 150; // 軌跡を保持するフレーム数（約2.5秒分）
};
