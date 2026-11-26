#pragma once
#include "Actor/Enemy.h"
#include "Framework/ActorRef.h"
#include "Framework/BaseActor.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "Render/Object3d/Object3d.h"
#include "Render/Sprite/Sprite.h"
#include "Render/Texture/TextureManager.h"
class SphereCollider;
class Player;
class SpriteRenderer;
class BossWeakPoint;
class BossCore;

enum class BossPhase { Phase1, PhaseTransition, Phase2, Dying, Defeated };

class Boss : public Enemy {
public:
  Boss();
  ~Boss() override;

  void Initialize() override;
  void InitializeUI(SpriteRenderer *spriteRenderer);
  void Update() override;
  void Draw3D() override;
  void Draw2D() override;

  // 突進中（予兆含む）かどうかの判定
  bool IsDashing() const {
      return currentState_ == BossState::DashTelegraph || currentState_ == BossState::Dash;
  }

  // 自機に当たっても自爆しない
  void OnHitPlayer() override {}

  // 画面の外でカメラの後ろに取り残されても消えない
  bool CanDespawnBehindCamera() const override { return false; }

  // 死亡演出中または死亡済みかどうかの判定
  bool IsDyingOrDefeated() const {
      return phase_ == BossPhase::Dying || phase_ == BossPhase::Defeated;
  }

  // 形態変化中かどうかの判定
  bool IsTransitioning() const {
      return phase_ == BossPhase::PhaseTransition;
  }

  bool HasActiveBits() const { return !activeBits_.empty(); }

  bool CanBeLockedOn() const override { return false; }

  // ステータスのゲッター/セッター
  int GetMaxHP() const { return maxHp_; }
  void SetMaxHP(int hp) { maxHp_ = hp; }
  BossPhase GetPhase() const { return phase_; }

  // カメラに関わる出来事（受け取ったシーンがカメラを動かす）
  enum class CameraEvent {
    CounterSucceeded,  // 突進中の弱点を全部壊された
    BarrageFired,      // 弾をばらまいた
    MissilesDeployed,  // ミサイルを出した
    DashStarted,       // 突進を始めた
    TransitionStarted, // 形態変化が始まった
    TransitionEnded,   // 形態変化が終わった
  };
  // 前に受け取ってから積んだ出来事を、起きた順に返して空にする
  std::vector<CameraEvent> TakeEvents() { return std::exchange(cameraEvents_, {}); }

  // 形態変化の進み（0〜1）。形態変化中でなければ 0
  float GetTransitionProgress() const;

  // ダメージを受ける処理
  void TakeDamage(int damage, bool isSelfDestruct = false) override;

  // 装甲・形態変化・HP のルールを通さずに倒す（デバッグ用。装甲と突進の的も消す）
  void ForceDefeat();

  // 撃破の見た目を外から進めるか。true の間は Defeated へ自分で移らない（FinishDying で移る）
  void SetDyingControlled(bool isControlled) { isDyingControlled_ = isControlled; }
  // 撃破の間の体の向き（ワールドの回転）。SetDyingControlled(true) の間に使う
  void SetDyingOrientation(const Quaternion &orientation);
  // 撃破の間に体を赤く光らせる（元の色から color へ amount の割合で寄せる。0 で元の色）
  void SetDyingFlash(const Vector4 &color, float amount);
  // 撃破を終える（Defeated へ移り、消える）
  void FinishDying();

  /// <summary>
  /// 撃破のときに爆発する、体の表面の点の表（モデルの座標。大きさ 1 のときの位置）。並びは爆発する順
  /// </summary>
  static const std::vector<Vector3> &GetBlastPoints();

private:
  void ChangePhase(BossPhase nextPhase);
  void UpdatePhase1();
  void UpdatePhaseTransition();
  void UpdatePhase2();
  void UpdateDying();
  // 撃破の間の体の色（元の色から赤く光らせる色へ、光らせる割合で寄せる）
  Vector4 GetDyingColor() const;
  // 残っている装甲と突進の的を消す
  void DestroyParts();
  void UpdateDashSequence(); // 突進の一連の処理（予兆・突進本体・スタン・終了後クールダウン）

  int maxHp_ = 100;
  BossPhase phase_ = BossPhase::Phase1;
  static constexpr float kTransitionSeconds_ = 3.5f; // 形態変化にかける秒数
  static constexpr Vector4 kCoreBlastColor_ = {1.0f, 0.5f, 0.0f, 1.0f}; // 撃破のときにコアが爆発する色
  std::vector<CameraEvent> cameraEvents_; // まだ受け取られていないカメラの出来事

  // ステートマシン管理
  enum class BossState {
    Enter,
    Hover,
    Telegraph,
    Attack,
    Cooldown,
    DashTelegraph,
    Dash,
    DashCooldown,
    Stagger // カウンター成功時のダウン状態
  };
  BossState currentState_ = BossState::Enter;
  float stateTimer_ = 0.0f;
  int attackStep_ = 0;
  int attackPattern_ = 0; // 0: 通常弾, 1: ミサイル, 2: 突進
  int dashCooldown_ = 0;  // 突進のクールダウン管理用
  Vector3 startPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 targetPos_ = {0.0f, 0.0f, 0.0f};

  // --- UI ---
  std::unique_ptr<Sprite> dangerUI_; // 突進時の警告UI
  bool isUIInitialized_ = false;

  // ミサイル用2Dロックオンレティクル (9パーツで構成: 外枠4, 内枠4, センター1)
  std::array<std::unique_ptr<Sprite>, 9> reticleSprites_;

  // Callback
  // --- 動的移動用のスプライン制御点 ---
  std::array<Vector3, 4> hoverWaypoints_;

  // --- 撃破の見た目 ---
  bool isDyingControlled_ = false;              // 撃破の見た目を外から進めているか
  Vector4 dyingFlashColor_ = {1.0f, 0.0f, 0.0f, 1.0f}; // 赤く光らせる色
  float dyingFlashAmount_ = 0.0f;               // 赤く光らせる割合（0〜1）
  std::function<void(const Vector3 &)>
      onDyingStartedCallback_; // Dyingフェーズに入った瞬間のコールバック

  // --- 部位（装甲）管理 ---
  std::vector<class BossBit *> activeBits_;
  
  // --- 突進時の弱点管理 ---
  std::vector<BossWeakPoint*> activeWeakPoints_;

  // --- コア（装甲の奥の弱点）。撃破のときに爆発させて消す ---
  std::vector<ActorRef<BossCore>> cores_;

public:
  // 最後の一撃（Dying に入った瞬間）に、ボスの位置を渡して呼ぶ
  void SetOnDyingStartedCallback(std::function<void(const Vector3 &)> cb) {
    onDyingStartedCallback_ = cb;
  }

  // ビットが死んだときにリストから削除するコールバック関数
  void OnBitDestroyed(BossBit *bit);
  
  // 突進時の弱点が死んだときにリストから削除し、全破壊を判定する
  void OnWeakPointDestroyed(BossWeakPoint* wp);
};
