#pragma once
#include "Actor/Enemy.h"
#include "Framework/BaseActor.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "Render/Object3d/Object3d.h"
#include "Render/Sprite/Sprite.h"
#include "Render/Texture/TextureManager.h"
class SphereCollider;
class ICamera;
class Player;
class SpriteRenderer;
class BossWeakPoint;

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
  void OnCollision(class Collider *other) override;

  // 突進中（予兆含む）かどうかの判定
  bool IsDashing() const {
      return currentState_ == BossState::DashTelegraph || currentState_ == BossState::Dash;
  }

  // ボスかどうかの判定
  bool IsBoss() const override { return true; }

  // 死亡演出中または死亡済みかどうかの判定
  bool IsDyingOrDefeated() const {
      return phase_ == BossPhase::Dying || phase_ == BossPhase::Defeated;
  }

  // 形態変化中かどうかの判定
  bool IsTransitioning() const {
      return phase_ == BossPhase::PhaseTransition;
  }

  bool HasActiveBits() const { return !activeBits_.empty(); }

  bool IsLockOnTarget() const override { return false; }

  // 表示用の3Dモデルを外から渡してセットする（overrideで確実にこちらが呼ばれる）
  void SetModel(std::unique_ptr<Object3d> model) override {
    model_ = std::move(model);
    // モデルが渡された後にディゾルブ用の設定を行う
    if (model_) {
      TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
      model_->SetMaskTexturePath("resources/noise0.png");
      model_->SetEnableDissolve(false);
      model_->SetDissolveThreshold(0.0f);
      model_->SetDissolveEdgeRange(0.1f); // DebugSceneのSuzanneと同じ値
      model_->SetDissolveEdgeColor(
          {0.0f, 5.0f, 5.0f, 1.0f}); // 強いネオンシアン
    }
  }
  Object3d *GetModel() const { return model_.get(); }
  void SetBaseColor(const Vector4 &color) { baseColor_ = color; }
  const Vector4 &GetBaseColor() const { return baseColor_; }

  void SetCamera(const ICamera *camera) { camera_ = camera; }
  void SetPlayer(const Player *player) { player_ = player; }

  Transform &GetTransform() { return transform_; }

  // ステータスのゲッター/セッター
  int GetHP() const { return hp_; }
  void SetHP(int hp) { hp_ = hp; }
  int GetMaxHP() const { return maxHp_; }
  void SetMaxHP(int hp) { maxHp_ = hp; }
  BossPhase GetPhase() const { return phase_; }

  // ダメージを受ける処理
  void TakeDamage(int damage, bool isSelfDestruct = false) override;

private:
  void ChangePhase(BossPhase nextPhase);
  void UpdatePhase1();
  void UpdatePhaseTransition();
  void UpdatePhase2();
  void UpdateDying();
  void UpdateDashSequence(); // 突進の一連の処理（予兆・突進本体・スタン・終了後クールダウン）

  int maxHp_ = 100;
  BossPhase phase_ = BossPhase::Phase1;

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

  // --- ディゾルブ制御 ---
  bool dissolveEnabled_ = false;

  float dyingTimer_ = 0.0f;    // 消滅演出の経過時間
  float dyingDuration_ = 3.0f; // 演出の総時間(秒)
  std::function<void(const Vector3 &)>
      onDyingUpdateCallback_; // Dyingフェーズの毎フレームコールバック

  // --- 部位（装甲）管理 ---
  std::vector<class BossBit *> activeBits_;
  
  // --- 突進時の弱点管理 ---
  std::vector<BossWeakPoint*> activeWeakPoints_;

public:
  void SetOnDyingUpdateCallback(std::function<void(const Vector3 &)> cb) {
    onDyingUpdateCallback_ = cb;
  }

  // ビットが死んだときにリストから削除するコールバック関数
  void OnBitDestroyed(BossBit *bit);
  
  // 突進時の弱点が死んだときにリストから削除し、全破壊を判定する
  void OnWeakPointDestroyed(BossWeakPoint* wp);
};
