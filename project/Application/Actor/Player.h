#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Framework/BaseActor.h"
#include "Math/Vector2.h"
#include "ReticleTunnel.h"
#include "Score/VolleyId.h"
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class LockOn;
class SpriteRenderer;
class ICamera;
class Object3d;
class DragonModel;
class Sprite;
class Input;
class Enemy;
class ScoreKeeper;

class Player : public BaseActor {
public:
  Player(const ICamera *camera);
  ~Player() override;

  void Initialize() override;
  void Update() override;
  void UpdateTransform() override;
  void ForceSnapToCamera();
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
  void SetCamera(const ICamera *camera) {
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
    float homingSpeed = 1.5f;              // ホーミング弾のスピード
    int homingFallTime = 165;              // 追尾を開始するまでのフレーム
    float homingStrengthIncrease = 0.015f; // 追尾のカーブの鋭さ
    float homingStrengthMax = 0.25f;       // 旋回力(追尾力)
    float reticleAcceleration = 2.5f;      // 照準の加速度
    float reticleFriction = 0.85f;         // 照準の摩擦力
    float reticleMaxSpeed = 25.0f;         // 照準の最高移動速度
    float playerFollowSpeed =
        0.12f; // 自機の目標座標への追従スピード（0.05は遅すぎたので0.12に）
    float playerDeadzone =
        0.05f; // 照準が動いても自機が移動しない遊び範囲（5%に縮小）
    float playerMaxMoveX =
        3.5f; // 自機の左右の最大移動範囲（画面端にいかないよう大幅に縮小）
    float playerMaxMoveY =
        2.0f; // 自機の上下の最大移動範囲（画面端にいかないよう大幅に縮小）
    float playerBaseOffsetY = -2.0f; // 自機の基本Yオフセット
    float rollStrength =
        7.0f; // ロールの強さ（可動域縮小による速度低下を補うため強化）
    float pitchStrength = 3.5f;    // ピッチの追加強度（強化）
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
  };
  ActionConfig &GetActionConfig() { return actionConfig_; }

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
  const ICamera *camera_ = nullptr;
  class Input *input_ = nullptr;
  std::vector<BaseActor *> lockOnTargets_;
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
    BaseActor *target = nullptr;
    int index = 0;
    int totalCount = 0;
    int volleyId = kNoVolleyId; // 射撃番号
  };
  std::vector<HomingQueueItem> homingQueue_;
  int homingFireTimer_ = 0;
  static constexpr int kHomingFireInterval = 3; // 3フレーム（約0.05秒）間隔で連続射出

  void UpdateHomingQueue();
  void SpawnHomingBullet(BaseActor *target, int index, int totalCount,
                         int volleyId);
  // 射撃の弾が 1 発終わったことを数える係へ知らせる
  void FinishVolleyBullet(int volleyId);
};
