#pragma once
#include "IEnemyBehavior.h"
#include "Actor/EnemyBullet.h"
#include "Math/Vector3.h"

class RailCamera;

/// <summary>
/// 固定砲台ビヘイビア（建造物・壁面に設置され、プレイヤーを感知・先読み迎撃するAI）
/// 画面に映るまでは休眠（Dormant）し、画面に映って陰から出ると起動（Active）して照準・迎撃を行う。
/// </summary>
class BehaviorTurret : public IEnemyBehavior {
public:
  struct Config {
    float fireDelay = 1.0f;        // 起動から初弾発射までの待機・照準時間（秒）
    float fireInterval = 1.0f;     // 次弾発射間隔（秒）
    float maxShotDistance = 280.0f; // 撃つのは自機からこの距離より近いときだけ（m）。敵の弾は自機から 300m 離れると消える
    float bulletSpeed = EnemyBullet::kAimedShotSpeed_; // 弾速（フレームあたり移動距離）
    float minFairDistance = 150.0f; // 撃つのは自機よりこの距離以上前にいるときだけ（m）
  };

  BehaviorTurret();
  explicit BehaviorTurret(const Config& config);
  ~BehaviorTurret() override = default;

  void Update(Enemy* enemy) override;
  void SetConfig(const Config& config) { config_ = config; }
  const Config& GetConfig() const { return config_; }

  // 弾が自機に届くころの、自機の位置を返す。
  // 自機はレールに沿って進み、レールから見た今のずれ（右・上・前）を保つとみなす
  // rail が nullptr か弾速が 0 以下なら、自機の今の位置を返す
  static Vector3 PredictPlayerPos(const RailCamera* rail, const Vector3& myPos, const Vector3& playerPos, float bulletSpeed);

private:
  enum class State {
    Dormant, // 休眠待機（画面に映るまで：初期向きを向いて沈黙）
    Active,  // 起動・照準（自機をエイム追尾）
    Combat   // 迎撃戦闘（射撃中）
  };

  static constexpr int kPredictIterations_ = 6; // 届く時間と届く位置を交互に求め直す回数

  Config config_;
  State state_ = State::Dormant;
  float activeTimer_ = 0.0f;
  float shotTimer_ = 0.0f;
  bool isInitialRotationSet_ = false;
  Vector3 restRotation_ = {0.0f, 0.0f, 0.0f};
};
