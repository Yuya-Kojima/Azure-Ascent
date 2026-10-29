#pragma once
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include <functional>
#include <memory>
#include <vector>
#include "Score/VolleyId.h"

class Object3d;
class Object3dRenderer;
class SphereCollider;
class Collider;

class HomingBullet : public BaseActor {
public:
  HomingBullet();
  ~HomingBullet() override;

  /// <summary>
  /// ホーミング弾の初期化
  /// </summary>
  /// <param name="renderer">3D描画用レンダラー</param>
  /// <param name="startPos">発射開始座標（自機の位置）</param>
  /// <param name="target">追従する対象</param>
  /// <param name="initialVelocity">発射直後の初速ベクトル（散らばり用）</param>
  void Initialize(Object3dRenderer* renderer, const Vector3& startPos, BaseActor* target, const Vector3& initialVelocity);
  
  // アクションエディタから受け取ったパラメータをセットする
  void SetHomingParams(float speed, int fallTime, float strengthIncrease, float strengthMax) {
    speed_ = speed;
    homingFallTime_ = fallTime;
    homingStrengthIncrease_ = strengthIncrease;
    homingStrengthMax_ = strengthMax;
  }

  // 弾消滅時（着弾・寿命切れ）のコールバックを設定
  void SetOnDestroyCallback(std::function<void(BaseActor*)> callback) {
    onDestroyCallback_ = callback;
  }

  // この弾の射撃番号と、弾が当たった・狙った敵を失った・消えたときに一度だけ呼ぶコールバック
  void SetVolley(int volleyId, std::function<void(int)> onFinished) {
    volleyId_ = volleyId;
    onVolleyFinished_ = onFinished;
  }

  void Update() override;
  void Draw3D() override;
  
  void OnCollision(Collider* other) override;

  BaseActor* GetTarget() const { return target_; }

private:
  std::unique_ptr<Object3d> object3d_;
  std::unique_ptr<SphereCollider> collider_;
  BaseActor* target_ = nullptr;
  BaseActor* originalTarget_ = nullptr;
  int damage_ = 5;
  
  Vector3 velocity_;
  float speed_ = 1.5f;           // 弾の飛ぶ速さ
  float homingStrength_ = 0.02f; // 誘導の強さ（小さいほど大回りする）
  int lifeTimer_ = 180;          // 寿命（約3秒で消滅）
  
  // アクションエディタ用パラメータ
  int homingFallTime_ = 165;
  float homingStrengthIncrease_ = 0.015f;
  float homingStrengthMax_ = 0.25f;

  Object3dRenderer* renderer_ = nullptr;

  // レーザートレイル（リボン軌跡）用パラメータ
  std::vector<Vector3> trailHistory_;
  static constexpr size_t kMaxTrailPoints = 35; // トレイル履歴の保持フレーム数（約0.6秒分）

  // 消滅時コールバック
  std::function<void(BaseActor*)> onDestroyCallback_;

  // 射撃番号と、射撃の弾が終わったことを知らせるコールバック
  int volleyId_ = kNoVolleyId;
  std::function<void(int)> onVolleyFinished_;
  bool hasFinishedVolley_ = false;

  // 射撃の弾が終わったことを一度だけ知らせる
  void FinishVolley();
};
