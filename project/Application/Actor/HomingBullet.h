#pragma once
#include "Framework/ActorRef.h"
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
class RailCamera;

// ロックオン弾: 始まり P0・ふくらみ P1・終わり P2（狙った敵の今の位置）を通る 2 次ベジェを、決めた秒でなぞって当たる
// P0・P1 はカメラの構図から見た位置で持ち、毎フレーム今の構図の上に置き直す
class HomingBullet : public BaseActor {
public:
  HomingBullet();
  ~HomingBullet() override;

  /// <summary>
  /// ロックオン弾の初期化
  /// </summary>
  /// <param name="renderer">3D描画用レンダラー</param>
  /// <param name="camera">P0・P1 を置く構図を持つカメラ</param>
  /// <param name="localStart">始まり P0 の、構図から見た位置（右・上・前の m）</param>
  /// <param name="localBulge">ふくらみ P1 の、構図から見た位置（右・上・前の m）</param>
  /// <param name="target">狙う敵（いなければ、P0 から P1 の向きへ同じだけ進んだ所で散る）</param>
  /// <param name="flightSeconds">撃ってから届くまでの秒</param>
  void Initialize(Object3dRenderer* renderer, const RailCamera* camera, const Vector3& localStart,
                  const Vector3& localBulge, BaseActor* target, float flightSeconds);

  // 弾が消えたとき（着弾・散ったとき）のコールバックを設定
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

  BaseActor* GetTarget() const { return target_.Get(); }

private:
  std::unique_ptr<Object3d> object3d_;
  std::unique_ptr<SphereCollider> collider_;
  ActorRef<BaseActor> target_;
  ActorRef<BaseActor> originalTarget_;
  int damage_ = 5;

  // ベジェの点と進み具合
  const RailCamera* camera_ = nullptr;
  Vector3 localStart_ = {0.0f, 0.0f, 0.0f}; // P0（構図から見た位置）
  Vector3 localBulge_ = {0.0f, 0.0f, 0.0f}; // P1（構図から見た位置）
  Vector3 endPoint_ = {0.0f, 0.0f, 0.0f};   // P2（ワールド。敵がいる間は毎フレーム敵の位置で読み直す）
  float flightSeconds_ = 0.45f;             // 撃ってから届くまでの秒
  float elapsedSeconds_ = 0.0f;             // 撃ってからの秒
  static constexpr float kFrameTime_ = 1.0f / 60.0f; // 1 フレームの秒数

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

  // ベジェの s（0〜1）の所のワールド位置
  Vector3 CalcPosition(float s) const;

  // 弾のモデルを進む向きへ回す
  void FaceDirection(const Vector3& direction);

  // 終わりの点に着いた: 敵がいればダメージを与え、着弾の演出を出して消える
  void Arrive();
};
