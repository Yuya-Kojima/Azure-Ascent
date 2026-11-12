#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Framework/ActorRef.h"
#include "Framework/BaseActor.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "ReticleTunnel.h"
#include "Score/VolleyId.h"
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class LockOn;
class SpriteRenderer;
class RailCamera;
class Object3d;
class DragonModel;
class Sprite;
class Input;
class Enemy;
class ScoreKeeper;

class Player : public BaseActor {
public:
  Player(const RailCamera *camera);
  ~Player() override;

  void Initialize() override;
  void Update() override;
  void UpdateTransform() override;
  void ForceSnapToCamera();
  /// <summary>
  /// 位置と見た目の向きを transform_ のまま使う（演出が自機の位置・向きを決めるとき。次の Update で、その位置・向きから操作に戻る）
  /// </summary>
  void UseExternalTransform();
  /// <summary>
  /// 見た目の向きを、角度 rotate の向きから飛ぶ向きへ ratio（0〜1）だけ混ぜた向きにする（このフレームだけ）
  /// </summary>
  void BlendOrientationFrom(const Vector3 &rotate, float ratio);
  void TakeDamage(int damage);
  // 無敵時間を無視して HP を 0 にする
  void Kill();
  void Draw3D() override;
  void Draw2D() override;
  void OnCollision(class Collider *other) override;

  bool IsLockOnMode() const;

  // シーンから情報を渡すためのセッター
  void SetSpriteRenderer(SpriteRenderer *renderer) {
    spriteRenderer_ = renderer;
  }
  void SetObject3dRenderer(class Object3dRenderer *renderer) {
    object3dRenderer_ = renderer;
  }
  void SetCamera(const RailCamera *camera) {
    assert(camera);
    camera_ = camera;
  }
  void SetInput(class Input *input) { input_ = input; }
  void SetModel(std::unique_ptr<Object3d> model) {
    object3d_ = std::move(model);
  }
  // 羽ばたく自機ドラゴンを使う（SetModel の Object3d より優先して描く）
  void SetDragonModel(std::unique_ptr<DragonModel> model);
  // 自機ドラゴン（なければ nullptr）
  DragonModel *GetDragonModel() const { return dragonModel_.get(); }

  /// <summary>
  /// 頭のワールド座標（ドラゴンのモデルがなければ自機の位置）
  /// </summary>
  Vector3 GetHeadPosition() const;

  /// <summary>
  /// 羽ばたきを進める（1フレームに1回）。deltaTime 0 で止めたまま更新する
  /// </summary>
  void UpdateWing(float deltaTime);

  /// <summary>
  /// 羽ばたきの状態を外から決める（開始演出など）。std::nullopt で自機の動きから決める状態に戻す
  /// </summary>
  void SetExternalWingState(
      const std::optional<DragonWingAnimator::State> &state) {
    externalWingState_ = state;
  }

  /// <summary>
  /// 羽ばたきに重ねる姿勢（開始演出の吠える姿など）。毎フレーム渡す
  /// </summary>
  void SetWingGesture(const DragonWingAnimator::Gesture &gesture);

  /// <summary>
  /// 狙われている光り具合（0〜1）で、体を赤く染める
  /// </summary>
  void SetThreatGlow(float amount);

  /// <summary>
  /// 翼の付け根の角度を外から決める（ゲームオーバーの演出）。空の関数で周期で打つ羽ばたきに戻す
  /// </summary>
  void SetDirectWingRoot(DragonWingAnimator::DirectRootFunction rootAngleAt);

  /// <summary>
  /// 羽ばたきの調整値の ImGui（ドラゴンのモデルがなければ何も出さない）
  /// </summary>
  void DrawWingConfigImGui();

  /// <summary>
  /// 片方の翼が今打ち下ろしている強さ（0〜1）。ドラゴンのモデルがなければ 0
  /// </summary>
  float GetWingPush(bool isRight) const;

  /// <summary>
  /// 片方の翼の付け根を今上げている角度（度）。ドラゴンのモデルがなければ 0
  /// </summary>
  float GetWingRootAngle(bool isRight) const;

  /// <summary>
  /// 顔の向き（度。DragonWingAnimator::GetFaceAngle の値）。ドラゴンのモデルがなければ 0
  /// </summary>
  float GetFaceAngle() const;
  void SetHitStopCallback(std::function<void(int)> callback) {
    hitStopCallback_ = callback;
  }

  // ロックオン可能な対象リストを外部から受け取る
  void SetLockOnTargets(const std::vector<BaseActor *> &targets) {
    lockOnTargets_ = targets;
  }
  // ロックオンはできず、通常ショットの狙いの助けだけが効く対象リストを外部から受け取る
  void SetShotTargets(const std::vector<BaseActor *> &targets) {
    shotTargets_ = targets;
  }
  // 敵を隠す箱を LockOn に渡す（LockOn::SetOccluders）
  void SetLockOnOccluders(const std::vector<Matrix4x4> *worldToBoxes);
  // カメラから pos への線が、敵を隠す箱のどれかを通るか
  bool IsHiddenFromCamera(const Vector3 &pos) const;

  int GetHp() const { return hp_; }
  int GetMaxHp() const { return kMaxHp; }

  // ロックオンの一斉射撃を数える係（nullptr なら数えない）
  void SetScoreKeeper(ScoreKeeper *scoreKeeper) { scoreKeeper_ = scoreKeeper; }

  // 今ロックしている（まだ撃っていない）数
  int GetLockingCount() const;

  // 照準の画面上の位置（1280x720 の px）
  Vector2 GetReticlePosition() const;

  bool IsDead() const { return isDead_; }

  // 照準の不透明度（0〜1）
  void SetReticleAlpha(float alpha);
  int GetInvincibleTimer() const { return invincibleTimer_; }

  // --- アクション・軌道調整用パラメータ ---
  struct ActionConfig {
    float lockOnRadius = 100.0f;           // ロックオンの吸いつき範囲
    float homingFlightSeconds = 0.55f;     // ロックオン弾が撃ってから届くまでの秒
    float homingBulgeFrames = 15.0f;       // ふくらみの点を、出口からカメラから見た初速で何フレーム進めた所に置くか
    float reticleAcceleration = 2.5f;      // 照準の加速度
    float reticleFriction = 0.85f;         // 照準の摩擦力
    float reticleMaxSpeed = 25.0f;         // 照準の最高移動速度
    float playerFollowTime = 0.5f;     // 自機が照準の示す位置の 9 割まで着く秒
    float playerFollowDamping = 0.7f;  // 自機の動きの減衰（1 で行き過ぎずに止まる。0.7 で約 4% 行き過ぎて戻る）
    float playerDeadzone =
        0.05f; // 照準が真ん中からこの割合（画面の端までを 1）より内にある間は、自機を動かさない
    float playerMaxMoveX =
        3.5f; // 自機の左右の最大移動範囲（m、画面の上で見えるずれ）
    float playerMaxMoveY =
        2.0f; // 自機の上下の最大移動範囲（m、画面の上で見えるずれ）
    float playerBaseOffsetY = -3.0f; // 照準が真ん中のときに、自機が画面の上で照準から下に見える量（m）
    float rollStrength =
        7.0f; // ロールの強さ（構図の中の左右の速さ m/フレーム に掛ける）
    float pitchStrength = 3.5f;    // ピッチの強さ（構図の中の上下の速さ m/フレーム に掛ける）
    float yawStrength = 2.5f;      // ヨーの追加強度（強化）
    float rollLerp = 0.15f;        // ロール補間速度
    float pitchLerp = 0.15f;       // ピッチ補間速度
    float yawLerp = 0.15f;         // ヨー補間速度
    float homingSpreadX = 0.3f;    // ホーミング弾の左右拡散幅
    float homingSpeedY = 0.6f;     // ホーミング弾の上方初速
    float homingSpeedZ = 0.8f;     // ホーミング弾の前方初速
    float normalShotSpeed = 10.0f; // 通常弾の弾速
    float muzzleOffsetX = 0.0f;       // 口元の左右オフセット
    float muzzleOffsetY = 0.5f;       // 口元の上方オフセット
    float muzzleOffsetForward = 2.5f; // 口元の前方オフセット
    // 羽ばたき（速度は1フレームあたりの移動量を、カメラの右・上の向きで見たもの）
    float wingStrength = 0.6f;       // 羽ばたきの基本の強さ
    float wingFrequency = 1.5f;      // 1秒あたりの羽ばたき回数
    float wingClimbStrength = 4.0f;  // 上向きの速度に掛けて強さに足す量
    float wingDiveFold = 6.0f;       // 下向きの速度に掛けて畳み具合にする量
    float wingTurnAsymmetry = 10.0f; // 左右の速度に掛けて左右差にする量
    float wingHover = 0.0f;          // 羽ばたきの姿勢の割合（0: 滑空 〜 1: 体を起こす）
    float wingClimbHover = 2.0f;     // 上向きの速度に掛けて羽ばたきの姿勢の割合に足す量
    // レールの曲がり・登りに合わせた姿勢と位置（曲がる速さは、レールが 1 秒に左右へ向きを変える角度）
    float railBankPerTurn = 4.0f;    // 曲がる速さ 1 度/秒あたりに、曲がる側へ傾ける角度（度）
    float railBankMax = 45.0f;       // レールの曲がりで傾ける最大の角度（度）
    float railClimbNose = 0.17f;     // レールの登りの角度に掛けて、機首を上げる角度にする割合
    float railLagPerTurn = 0.133f;   // 曲がる速さ 1 度/秒あたりに、画面の上でカーブの外側へ流れる量（m）
    float railLagPerClimb = 0.014f;  // 登りの角度 1 度あたりに、画面の上で下へ遅れる量（m）
    float railFollow = 0.03f;        // 曲がる速さ・登りの角度が、測った値へ近づく割合（1 フレーム）
  };
  ActionConfig &GetActionConfig() { return actionConfig_; }
  // なめらかにしたレールの曲がる速さ（度/秒、右が正）と登りの角度（度）
  float GetRailTurnRate() const { return railTurnRate_; }
  float GetRailClimb() const { return railClimb_; }
  // 構図の中の自機の位置（右・上、m。レールの曲がりの流れは含まない）
  const Vector2 &GetFramingOffset() const { return framingOffset_; }

  void SaveActionConfig();
  void LoadActionConfig();

  bool IsActionConfigDirty() const { return isActionConfigDirty_; }
  void SetActionConfigDirty(bool dirty) { isActionConfigDirty_ = dirty; }

  // 初期化時に設定ファイルをロードするか制御するフラグのセッター
  void SetLoadConfigOnInitialize(bool load) { loadConfigOnInitialize_ = load; }

private:
  ActionConfig actionConfig_;
  bool isActionConfigDirty_ = false;
  bool loadConfigOnInitialize_ = true;

  // HP
  static constexpr int kMaxHp = 10;
  int hp_ = kMaxHp;

  std::unique_ptr<class SphereCollider> collider_;
  std::unique_ptr<LockOn> lockOn_;
  std::shared_ptr<bool> isAliveToken_ = std::make_shared<bool>(true);

  SpriteRenderer *spriteRenderer_ = nullptr;
  class Object3dRenderer *object3dRenderer_ = nullptr;
  const RailCamera *camera_ = nullptr;
  class Input *input_ = nullptr;
  std::vector<BaseActor *> lockOnTargets_;
  std::vector<BaseActor *> shotTargets_;
  ScoreKeeper *scoreKeeper_ = nullptr;

  void FireHomingShot();
  void FireNormalShot(); // 追加

  // 3D視線トンネル照準
  std::unique_ptr<ReticleTunnel> reticleTunnel_;

  // 通常弾の狙い：レティクルに重なる敵がいればその敵、いなければこの距離の点を狙う
  static constexpr float kAimAssistScreenRadius_ = 40.0f;   // レティクルに重なっているとみなす画面上の半径（ピクセル、1280x720基準）
  static constexpr float kAimConvergenceDistance_ = 150.0f; // 敵がいないときに弾がレティクルと重なる距離（カメラから）

  float flashIntensity_ = 0.0f;             // 画面フラッシュ強度
  Vector3 flashColor_ = {1.0f, 0.0f, 0.0f}; // 画面フラッシュカラー

  int invincibleTimer_ = 0; // 無敵時間のタイマー

  std::function<void(int)> hitStopCallback_ =
      nullptr; // ヒットストップ要求コールバック



  // 自機の3Dモデル（dragonModel_ があればそちらを使う）
  std::unique_ptr<Object3d> object3d_;
  std::unique_ptr<DragonModel> dragonModel_;
  // 位置・向きを設定して描く Object3d
  Object3d *GetBodyObject() const;

  static constexpr Vector4 kThreatBodyColor_ = {2.0f, 0.35f, 0.3f, 1.0f}; // 狙われている光がいちばん強いときの体の色
  static constexpr Vector3 kThreatRimColor_ = {1.0f, 0.1f, 0.05f};        // 狙われている間の体のふちの光の色
  static constexpr float kThreatBodyMix_ = 0.7f;                          // 体の色を kThreatBodyColor_ へ寄せる最大の割合
  static constexpr float kThreatRimIntensity_ = 2.0f;                     // 体のふちの光のいちばん強い明るさ
  static constexpr float kThreatRimPower_ = 2.5f;                         // 体のふちの光の広がり（大きいほど細い）

  // 飛んでいる間の見た目の向き（前はレールの前で、照準が首の届く角度の外ならその分だけ照準の方へ。上はレールの上。動きの傾きは自機の軸のまわり）
  Quaternion flightOrientation_{0.0f, 0.0f, 0.0f, 1.0f};
  bool useFlightOrientation_ = false; // true の間は flightOrientation_ で描く（false なら transform_.rotate）
  // 開始演出から操作へ移る間に描く向き（BlendOrientationFrom で決め、次の Update で消す）
  std::optional<Quaternion> blendedOrientation_;
  // 構図の中の自機の位置（右・上、m）。構図が動いても、この位置のまま一緒に運ばれる
  Vector2 framingOffset_ = {0.0f, 0.0f};
  bool hasFramingOffset_ = false; // false なら次の Update で今の位置から測り直す
  Vector2 framingVelocity2D_ = {0.0f, 0.0f}; // 構図の中の自機の速さ（右・上、m/秒）
  // ばねの固有角振動数 = この値 / playerFollowTime（減衰 0.7 のとき、目標の 9 割に着く秒が playerFollowTime になる）
  static constexpr float kFollowSettleFactor_ = 2.6f;
  // レールの曲がる速さ（度/秒、右が正）と登りの角度（度、上が正）。どちらもなめらかにした値
  float railTurnRate_ = 0.0f;
  float railClimb_ = 0.0f;
  Vector3 prevRailForward_ = {0.0f, 0.0f, 1.0f};
  bool hasPrevRailForward_ = false;
  // 1 フレームで測る曲がる速さの上限（度/秒）。t を飛ばしたときの向きの跳びを数えない
  static constexpr float kMaxRailTurnRate_ = 30.0f;

  // 自機の動きから決めた羽ばたきの状態と、外から決める状態
  DragonWingAnimator::State flightWingState_;
  std::optional<DragonWingAnimator::State> externalWingState_;

  // 攻撃用ステート
  enum class AttackState {
    Idle,     // 待機
    Pressing, // 押下中（短押しか長押しか見極め中）
    LockOn    // ロックオンモード（長押し確定）
  };
  AttackState attackState_ = AttackState::Idle;
  float pressTimer_ = 0.0f;

  // ホーミング弾シーケンシャル発射キュー
  struct HomingQueueItem {
    ActorRef<BaseActor> target;
    int index = 0;
    int totalCount = 0;
    int volleyId = kNoVolleyId; // 射撃番号
  };
  std::vector<HomingQueueItem> homingQueue_;
  int homingFireTimer_ = 0;
  static constexpr int kHomingFireInterval = 3; // 3フレーム（約0.05秒）間隔で連続射出

  // カメラの構図が 1 フレームに進んだ量（m）。ロックオン弾のカメラから見た初速に使う
  Vector3 prevFramingPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 framingVelocity_ = {0.0f, 0.0f, 0.0f};
  bool hasPrevFramingPos_ = false;

  void UpdateHomingQueue();
  void SpawnHomingBullet(BaseActor *target, int index, int totalCount,
                         int volleyId);
  // 射撃の弾が 1 発終わったことを数える係へ知らせる
  void FinishVolleyBullet(int volleyId);
};
