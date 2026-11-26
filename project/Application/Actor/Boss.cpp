#include "Actor/Boss.h"
#include "Audio/SoundManager.h"
#include "Actor/BossBit.h"
#include "Actor/BossCore.h"
#include "Actor/BossWeakPoint.h"
#include "Actor/EnemyBullet.h"
#include "Actor/IClearedByBoss.h"
#include "Actor/Player.h"
#include "Framework/ActorManager.h" 
#include "Camera/ICamera.h"
#include "Camera/RailCamera.h"
#include "Collision/CollisionConfig.h"
#include "Collision/CollisionManager.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Effect/EffectManager.h"
#include "Framework/ActorManager.h"
#include "Framework/GameManager.h"
#include "Framework/PrefabManager.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Texture/TextureManager.h"
#include <algorithm>
#include <optional>
#include <cmath>
#include <numbers>

//=======================
// 動き
//=======================

/// <summary>
/// 動きの基底クラス。毎フレームの動きと次の動きを決め、ボスの今の様子に答える
/// </summary>
class Boss::ActionState {
public:
  explicit ActionState(Boss &boss) : boss_(boss) {}
  virtual ~ActionState() = default;

  /// <summary>
  /// この動きに入ったときの準備
  /// </summary>
  virtual void Enter() {}

  /// <summary>
  /// 毎フレームの更新。次の動きへ移るなら、その動きを返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<ActionState> Update() { return nullptr; }

  /// <summary>
  /// 突進の的を全部壊された。次の動きへ移るなら、その動きを返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<ActionState> OnAllWeakPointsDestroyed() { return nullptr; }

  /// <summary>
  /// 形態の動きより先に進めるか（突進の一連は、形態に関わらず最後まで進める）
  /// </summary>
  virtual bool OverridesPhase() const { return false; }

  /// <summary>
  /// 突進中（予兆を含む）か
  /// </summary>
  virtual bool IsDashing() const { return false; }

  /// <summary>
  /// 予兆の体の色（予兆でなければなし）
  /// </summary>
  virtual std::optional<Vector4> GetWarningColor() const { return std::nullopt; }

  /// <summary>
  /// 突進の警告の帯を出すか
  /// </summary>
  virtual bool ShowsDangerWarning() const { return false; }

  /// <summary>
  /// ミサイルの照準を自機に出すか
  /// </summary>
  virtual bool ShowsMissileReticle() const { return false; }

protected:
  Boss &GetBoss() const { return boss_; }

private:
  Boss &boss_;
};

/// <summary>
/// 何もしない（形態変化・撃破の間など、形態が動きを決めている間）
/// </summary>
class Boss::IdleAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
};

/// <summary>
/// 第 1 形態の登場。カメラの前の基準の位置に置いたら滞空へ
/// </summary>
class Boss::Phase1EnterAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 1 形態の滞空。曲線に沿って移り終えたら予兆へ
/// </summary>
class Boss::Phase1HoverAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 1 形態の予兆。震えて溜め終えたら攻撃へ
/// </summary>
class Boss::Phase1TelegraphAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
  std::optional<Vector4> GetWarningColor() const override { return kTelegraphColor_; }
  bool ShowsMissileReticle() const override;
};

/// <summary>
/// 第 1 形態の攻撃。扇状の弾かミサイルを撃ったらクールダウンへ
/// </summary>
class Boss::Phase1AttackAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 1 形態のクールダウン。次の攻撃を選び、突進なら突進の予兆へ、ほかは滞空へ
/// </summary>
class Boss::Phase1CooldownAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 2 形態の滞空。高速に移り終えたら、選んだ攻撃の予兆へ
/// </summary>
class Boss::Phase2HoverAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 2 形態の予兆（いつもミサイル）。激しく震えて溜め終えたら攻撃へ
/// </summary>
class Boss::Phase2TelegraphAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
  std::optional<Vector4> GetWarningColor() const override { return kTelegraphColor_; }
  bool ShowsMissileReticle() const override { return true; }
};

/// <summary>
/// 第 2 形態の攻撃。ミサイルを円形に出したらクールダウンへ
/// </summary>
class Boss::Phase2AttackAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 第 2 形態のクールダウン。次の攻撃を選んで、その攻撃の位置へ滞空する
/// </summary>
class Boss::Phase2CooldownAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
};

/// <summary>
/// 突進の予兆。装甲を広げて的を出し、奥へ下がって震え、溜め終えたら突進へ。的を全部壊されたらダウンへ
/// </summary>
class Boss::DashTelegraphAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
  std::unique_ptr<ActionState> OnAllWeakPointsDestroyed() override;
  bool OverridesPhase() const override { return true; }
  bool IsDashing() const override { return true; }
  std::optional<Vector4> GetWarningColor() const override { return kDashTelegraphColor_; }
  bool ShowsDangerWarning() const override { return true; }
};

/// <summary>
/// 突進。画面を通り抜けたら突進のあとへ。的を全部壊されたらダウンへ
/// </summary>
class Boss::DashAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
  std::unique_ptr<ActionState> OnAllWeakPointsDestroyed() override;
  bool OverridesPhase() const override { return true; }
  bool IsDashing() const override { return true; }
};

/// <summary>
/// ダウン（カウンター成功）。弾き飛ばされて戻ったら、今の形態のクールダウンへ
/// </summary>
class Boss::StaggerAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  void Enter() override { GetBoss().BeginStagger(); }
  std::unique_ptr<ActionState> Update() override;
  bool OverridesPhase() const override { return true; }
};

/// <summary>
/// 突進のあと。隙を晒し終えたら、今の形態のクールダウンへ
/// </summary>
class Boss::DashCooldownAction : public Boss::ActionState {
public:
  using ActionState::ActionState;
  std::unique_ptr<ActionState> Update() override;
  bool OverridesPhase() const override { return true; }
};

//=======================
// 形態
//=======================

/// <summary>
/// 形態の基底クラス。突進以外の毎フレームの動きと次の形態を決め、ボスの今の様子に答える
/// </summary>
class Boss::PhaseState {
public:
  explicit PhaseState(Boss &boss) : boss_(boss) {}
  virtual ~PhaseState() = default;

  /// <summary>
  /// この形態に入ったときの準備
  /// </summary>
  virtual void Enter() {}

  /// <summary>
  /// 突進以外の毎フレームの動き。次の形態へ移るなら、その形態を返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<PhaseState> Update() { return nullptr; }

  /// <summary>
  /// ダメージを受けた。次の形態へ移るなら、その形態を返す（このままなら nullptr）。HP がなくなったら撃破の間へ
  /// </summary>
  virtual std::unique_ptr<PhaseState> OnDamaged();

  /// <summary>
  /// 撃破の見た目を外から進め終えた。次の形態へ移るなら、その形態を返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<PhaseState> OnDyingFinished() { return nullptr; }

  /// <summary>
  /// ダメージを受け付けるか
  /// </summary>
  virtual bool AcceptsDamage() const { return false; }

  /// <summary>
  /// 突進が終わったあとに入る、この形態のクールダウンの動き
  /// </summary>
  virtual std::unique_ptr<ActionState> CreateCooldown() {
    return std::make_unique<IdleAction>(GetBoss());
  }

  /// <summary>
  /// 体の色を、撃破の間の色にするか
  /// </summary>
  virtual bool UsesDyingColor() const { return false; }

  /// <summary>
  /// 形態変化中か
  /// </summary>
  virtual bool IsTransitioning() const { return false; }

  /// <summary>
  /// 撃破の間か、撃破済みか
  /// </summary>
  virtual bool IsDyingOrDefeated() const { return false; }

protected:
  Boss &GetBoss() const { return boss_; }

private:
  Boss &boss_;
};

/// <summary>
/// 撃破済み。入ったときに倒されたことを知らせて消える
/// </summary>
class Boss::DefeatedState : public Boss::PhaseState {
public:
  using PhaseState::PhaseState;
  void Enter() override { GetBoss().EnterDefeated(); }
  bool IsDyingOrDefeated() const override { return true; }
};

/// <summary>
/// 撃破の間。撃破の見た目を外から進め終えたら（クリア演出の外で倒れたときはすぐに）撃破済みへ
/// </summary>
class Boss::DyingState : public Boss::PhaseState {
public:
  using PhaseState::PhaseState;

  void Enter() override { GetBoss().EnterDying(); }

  std::unique_ptr<PhaseState> Update() override {
    if (GetBoss().UpdateDying()) {
      return std::make_unique<DefeatedState>(GetBoss());
    }
    return nullptr;
  }

  std::unique_ptr<PhaseState> OnDyingFinished() override {
    return std::make_unique<DefeatedState>(GetBoss());
  }

  bool UsesDyingColor() const override { return true; }
  bool IsDyingOrDefeated() const override { return true; }
};

std::unique_ptr<Boss::PhaseState> Boss::PhaseState::OnDamaged() {
  if (GetBoss().GetHP() <= 0) {
    return std::make_unique<DyingState>(GetBoss());
  }
  return nullptr;
}

/// <summary>
/// 第 2 形態。高速・広範囲に動き、攻撃の予兆はいつもミサイル
/// </summary>
class Boss::Phase2State : public Boss::PhaseState {
public:
  using PhaseState::PhaseState;

  void Enter() override { GetBoss().EnterPhase2(); }

  std::unique_ptr<PhaseState> Update() override {
    GetBoss().UpdateAction();
    return nullptr;
  }

  std::unique_ptr<ActionState> CreateCooldown() override {
    return std::make_unique<Phase2CooldownAction>(GetBoss());
  }

  bool AcceptsDamage() const override { return true; }
};

/// <summary>
/// 形態変化。画面奥へ退いて揺れ・点滅し、終わったら第 2 形態へ。ダメージは受け付けない
/// </summary>
class Boss::TransitionState : public Boss::PhaseState {
public:
  using PhaseState::PhaseState;

  void Enter() override { GetBoss().EnterTransition(); }

  std::unique_ptr<PhaseState> Update() override {
    if (GetBoss().UpdatePhaseTransition()) {
      return std::make_unique<Phase2State>(GetBoss());
    }
    return nullptr;
  }

  bool IsTransitioning() const override { return true; }
};

/// <summary>
/// 第 1 形態。HP が決まった割合まで減ったら形態変化へ
/// </summary>
class Boss::Phase1State : public Boss::PhaseState {
public:
  using PhaseState::PhaseState;

  std::unique_ptr<PhaseState> Update() override {
    GetBoss().UpdateAction();
    return nullptr;
  }

  std::unique_ptr<ActionState> CreateCooldown() override {
    return std::make_unique<Phase1CooldownAction>(GetBoss());
  }

  std::unique_ptr<PhaseState> OnDamaged() override {
    if (std::unique_ptr<PhaseState> next = PhaseState::OnDamaged()) {
      return next;
    }
    const Boss &boss = GetBoss();
    if (boss.GetHP() <= boss.maxHp_ * kPhase2HpRatio_) {
      return std::make_unique<TransitionState>(GetBoss());
    }
    return nullptr;
  }

  bool AcceptsDamage() const override { return true; }
};

Boss::Boss()
    : phase_(std::make_unique<Phase1State>(*this)),
      action_(std::make_unique<Phase1EnterAction>(*this)) {}

//=======================
// 動きの移り変わり
//=======================

std::unique_ptr<Boss::ActionState> Boss::Phase1EnterAction::Update() {
  GetBoss().PlaceAtSpawn();
  return std::make_unique<Phase1HoverAction>(GetBoss());
}

std::unique_ptr<Boss::ActionState> Boss::Phase1HoverAction::Update() {
  if (GetBoss().UpdatePhase1Hover()) {
    return std::make_unique<Phase1TelegraphAction>(GetBoss());
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::Phase1TelegraphAction::Update() {
  if (GetBoss().UpdatePhase1Telegraph()) {
    return std::make_unique<Phase1AttackAction>(GetBoss());
  }
  return nullptr;
}

bool Boss::Phase1TelegraphAction::ShowsMissileReticle() const {
  return GetBoss().attackPattern_ == kPatternMissile_;
}

std::unique_ptr<Boss::ActionState> Boss::Phase1AttackAction::Update() {
  GetBoss().FirePhase1Barrage();
  return std::make_unique<Phase1CooldownAction>(GetBoss());
}

std::unique_ptr<Boss::ActionState> Boss::Phase1CooldownAction::Update() {
  Boss &boss = GetBoss();
  if (!boss.UpdatePhase1Cooldown()) {
    return nullptr;
  }
  if (boss.attackPattern_ == kPatternDash_) {
    return std::make_unique<DashTelegraphAction>(boss);
  }
  return std::make_unique<Phase1HoverAction>(boss);
}

std::unique_ptr<Boss::ActionState> Boss::Phase2HoverAction::Update() {
  Boss &boss = GetBoss();
  if (!boss.UpdatePhase2Hover()) {
    return nullptr;
  }
  if (boss.attackPattern_ == kPatternMissile_) {
    return std::make_unique<Phase2TelegraphAction>(boss);
  }
  if (boss.attackPattern_ == kPatternDash_) {
    return std::make_unique<DashTelegraphAction>(boss);
  }
  return std::make_unique<Phase2HoverAction>(boss);
}

std::unique_ptr<Boss::ActionState> Boss::Phase2TelegraphAction::Update() {
  if (GetBoss().UpdatePhase2Telegraph()) {
    return std::make_unique<Phase2AttackAction>(GetBoss());
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::Phase2AttackAction::Update() {
  GetBoss().FirePhase2Missiles();
  return std::make_unique<Phase2CooldownAction>(GetBoss());
}

std::unique_ptr<Boss::ActionState> Boss::Phase2CooldownAction::Update() {
  if (GetBoss().UpdatePhase2Cooldown()) {
    return std::make_unique<Phase2HoverAction>(GetBoss());
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::DashTelegraphAction::Update() {
  if (GetBoss().UpdateDashTelegraph()) {
    return std::make_unique<DashAction>(GetBoss());
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::DashTelegraphAction::OnAllWeakPointsDestroyed() {
  return std::make_unique<StaggerAction>(GetBoss());
}

std::unique_ptr<Boss::ActionState> Boss::DashAction::Update() {
  if (GetBoss().UpdateDash()) {
    return std::make_unique<DashCooldownAction>(GetBoss());
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::DashAction::OnAllWeakPointsDestroyed() {
  return std::make_unique<StaggerAction>(GetBoss());
}

std::unique_ptr<Boss::ActionState> Boss::StaggerAction::Update() {
  if (GetBoss().UpdateStagger()) {
    return GetBoss().phase_->CreateCooldown();
  }
  return nullptr;
}

std::unique_ptr<Boss::ActionState> Boss::DashCooldownAction::Update() {
  // 突進後、決まった秒数だけ隙を晒す
  if (GetBoss().stateTimer_ >= kDashCooldownSeconds_) {
    return GetBoss().phase_->CreateCooldown();
  }
  return nullptr;
}

Boss::~Boss() {}

void Boss::Initialize() {
  Enemy::Initialize();
  SetColliderRadius(0.4f); // コアより判定を小さくして、弾がボス本体に吸われないようにする
  SetHP(500);
  maxHp_ = 500;
  phase_ = std::make_unique<Phase1State>(*this);
  isDead_ = false;

  action_ = std::make_unique<Phase1EnterAction>(*this);
  stateTimer_ = 0.0f;
  startPos_ = {0.0f, 0.0f, 0.0f};
  targetPos_ = {0.0f, 0.0f, 0.0f};

  ResetAliveTime();
  isDyingControlled_ = false;
  dyingFlashAmount_ = 0.0f;

  // --- コアと装甲の生成 ---
  float bitOffsetRadius = 0.6f;

  // Z軸を手前にずらす。コアは奥、装甲は手前。
  Vector3 coreOffsets[4] = {
      {0.0f, bitOffsetRadius, -0.6f},  // 上
      {0.0f, -bitOffsetRadius, -0.6f}, // 下
      {-bitOffsetRadius, 0.0f, -0.6f}, // 左
      {bitOffsetRadius, 0.0f, -0.6f}   // 右
  };

  Vector3 bitOffsets[4] = {
      {0.0f, bitOffsetRadius, -1.2f},  // 上
      {0.0f, -bitOffsetRadius, -1.2f}, // 下
      {-bitOffsetRadius, 0.0f, -1.2f}, // 左
      {bitOffsetRadius, 0.0f, -1.2f}   // 右
  };

  for (int i = 0; i < 4; ++i) {
    // 1. コアの生成（奥側）
    auto core = std::make_unique<BossCore>();
    auto coreModel = std::make_unique<Object3d>();
    coreModel->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
    coreModel->SetModel("__builtin_sphere");
    coreModel->SetScale({0.4f, 0.4f, 0.4f});
    coreModel->SetColor({1.0f, 0.1f, 0.2f, 1.0f}); // 赤色オーブ
    core->SetModel(std::move(coreModel));

    core->SetBoss(this);
    core->SetOffset(coreOffsets[i]);

    BossCore *corePtr = core.get();
    cores_.push_back(ActorRef<BossCore>(corePtr));
    ActorManager::GetInstance()->AddActor(std::move(core));

    // 2. 装甲の生成（手前側）
    auto bit = std::make_unique<BossBit>();
    auto bitModel = std::make_unique<Object3d>();
    bitModel->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
    bitModel->SetModel("__builtin_box");
    bitModel->SetScale({0.8f, 1.2f, 0.15f}); // 薄型シールド装甲板
    bitModel->SetColor({0.9f, 0.95f, 1.0f, 1.0f}); // 白銀シールド
    bit->SetModel(std::move(bitModel));

    bit->SetBoss(this);
    bit->SetOffset(bitOffsets[i]);
    bit->SetId(i);

    BossBit *bitPtr = bit.get();
    bit->SetOnDestroyedCallback(
        [this, bitPtr](bool) { this->OnBitDestroyed(bitPtr); });

    // コアに装甲をセットする
    corePtr->SetShield(bitPtr);

    activeBits_.push_back(bitPtr);
    ActorManager::GetInstance()->AddActor(std::move(bit));
  }
}

void Boss::OnBitDestroyed(BossBit *bit) {
  auto it = std::find(activeBits_.begin(), activeBits_.end(), bit);
  if (it != activeBits_.end()) {
    activeBits_.erase(it);
  }
}

void Boss::OnWeakPointDestroyed(BossWeakPoint *wp) {
  // 的が破壊されたら、同じIDを持つ装甲（BossBit）にボーナスダメージを与える
  int targetId = wp->GetId();
  for (auto* bit : activeBits_) {
      if (bit->GetId() == targetId) {
          bit->TakeDamage(15);
          break; 
      }
  }

  auto it = std::find(activeWeakPoints_.begin(), activeWeakPoints_.end(), wp);
  if (it != activeWeakPoints_.end()) {
    activeWeakPoints_.erase(it);
  }

  // 全て破壊されたら今の動きに知らせ、次の動きが返ってきたら入れ替える（突進中・予兆中ならカウンター成功）
  if (activeWeakPoints_.empty()) {
    if (std::unique_ptr<ActionState> next = action_->OnAllWeakPointsDestroyed()) {
      ChangeAction(std::move(next));
    }
  }
}

void Boss::BeginStagger() {
  SoundManager::GetInstance()->PlaySE("boss_counter");

  // 装甲を元に戻す
  for (auto* bit : activeBits_) {
      bit->ResetPosition();
  }

  cameraEvents_.push_back(CameraEvent::CounterSucceeded);
  EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);
}

void Boss::InitializeUI(SpriteRenderer *spriteRenderer) {
  if (!spriteRenderer)
    return;

  dangerUI_ = std::make_unique<Sprite>();
  dangerUI_->Initialize(spriteRenderer, "resources/white1x1.png"); 
  dangerUI_->SetAnchorPoint({0.5f, 0.5f});
  dangerUI_->SetPosition({1280.0f / 2.0f, 720.0f / 2.0f});
  dangerUI_->SetSize({600.0f, 100.0f}); // 大きな警告バー
  dangerUI_->SetColor({1.0f, 0.0f, 0.0f, 0.5f}); // 半透明の赤

  for (size_t i = 0; i < reticleSprites_.size(); ++i) {
    reticleSprites_[i] = std::make_unique<Sprite>();
    reticleSprites_[i]->Initialize(spriteRenderer, "resources/white1x1.png"); // 全て四角ベースにする
    reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
    reticleSprites_[i]->SetAnchorPoint({0.5f, 0.5f});
  }

  isUIInitialized_ = true;
}

void Boss::Update() {
  if (isDead_) {
    return;
  }

  // 被弾時のフラッシュ処理
  if (TickHitFlash()) {
    if (GetModel()) {
      GetModel()->SetColor({1.0f, 0.0f, 0.0f, 1.0f}); // 赤色に点滅
    }
  } else {
    // 状態に応じたカラーの適用
    if (GetModel()) {
      if (std::optional<Vector4> warningColor = action_->GetWarningColor()) {
        GetModel()->SetColor(*warningColor); // 予兆の色
      } else if (phase_->UsesDyingColor()) {
        GetModel()->SetColor(GetDyingColor());
      } else {
        GetModel()->SetColor(GetBaseColor());
      }
    }
  }

  AdvanceAliveTime();

  // 突進の一連は形態に関わらず進め、それ以外は今の形態が進める（次の形態が返ってきたら入れ替える）
  if (action_->OverridesPhase()) {
    UpdateAction();
  } else if (std::unique_ptr<PhaseState> next = phase_->Update()) {
    ChangePhase(std::move(next));
  }

  // 最後にトランスフォーム（モデル・コライダー位置）を更新する
  UpdateTransform();
}

bool Boss::UpdatePhaseTransition() {
  stateTimer_ += 1.0f / 60.0f;
  const float duration = kTransitionSeconds_;

  // 1. ボスの退避移動 
  float moveDuration = duration - 0.5f; // 最後の0.5秒はバースト用
  float moveT = std::min(stateTimer_ / moveDuration, 1.0f);
  float easeT = moveT * moveT * (3.0f - 2.0f * moveT);

  transform_.translate.x = std::lerp(startPos_.x, targetPos_.x, easeT);
  transform_.translate.y = std::lerp(startPos_.y, targetPos_.y, easeT);
  transform_.translate.z = std::lerp(startPos_.z, targetPos_.z, easeT);

  // 2. ボスのシェイクとカラー点滅
  float shakePower = 1.0f * (stateTimer_ / duration);
  float rx = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  float ry = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  float rz = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  transform_.translate.x += rx;
  transform_.translate.y += ry;
  transform_.translate.z += rz;

  float flashSpeed = 10.0f + 30.0f * (stateTimer_ / duration);
  float flashIntensity = (std::sin(stateTimer_ * flashSpeed) + 1.0f) * 0.5f;
  if (GetModel()) {
      Vector4 flashColor = {1.0f, 1.0f - flashIntensity * 0.8f, 1.0f - flashIntensity * 0.8f, 1.0f};
      GetModel()->SetColor(flashColor);
  }

  // 3. 移行完了でPhase2へ
  if (stateTimer_ >= duration) {
      if (GetModel()) {
          GetModel()->SetColor(GetBaseColor());
      }
      cameraEvents_.push_back(CameraEvent::TransitionEnded);
      EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);
      return true;
  }
  return false;
}

bool Boss::UpdateDying() {
  // 撃破の見た目を外から進めていなければ（クリア演出の外で倒れたとき）、その場で爆発して消える
  if (!isDyingControlled_) {
    EffectManager::GetInstance()->PlayEnemyDeathEffect(transform_.translate);
    return true;
  }
  return false;
}

Vector4 Boss::GetDyingColor() const {
  const float t = std::clamp(dyingFlashAmount_, 0.0f, 1.0f);
  return {GetBaseColor().x + (dyingFlashColor_.x - GetBaseColor().x) * t,
          GetBaseColor().y + (dyingFlashColor_.y - GetBaseColor().y) * t,
          GetBaseColor().z + (dyingFlashColor_.z - GetBaseColor().z) * t,
          GetBaseColor().w};
}

void Boss::SetDyingOrientation(const Quaternion &orientation) {
  // 世界が止まっていて Update が呼ばれない間も、見た目へすぐ渡す
  if (GetModel()) {
    GetModel()->SetRotationQuaternion(orientation);
    GetModel()->Update();
  }
}

void Boss::SetDyingFlash(const Vector4 &color, float amount) {
  dyingFlashColor_ = color;
  dyingFlashAmount_ = amount;
  if (GetModel() && !IsHitFlashing()) {
    GetModel()->SetColor(GetDyingColor());
  }
}

const std::vector<Vector3> &Boss::GetBlastPoints() {
  // クリスタル（半径 0.5 の六角の胴、前後の尖端が z = ±1）の面の上の点。前後・上下・左右へ交互に散らす
  static const std::vector<Vector3> kPoints = {
      {0.000f, 0.281f, 0.350f},   {-0.035f, -0.303f, -0.300f},
      {0.180f, -0.078f, 0.550f},  {-0.181f, 0.119f, -0.500f},
      {0.290f, 0.191f, -0.200f},  {-0.281f, -0.162f, 0.250f},
      {-0.030f, -0.173f, 0.600f}, {-0.027f, 0.238f, -0.450f},
  };
  return kPoints;
}

void Boss::FinishDying() {
  if (std::unique_ptr<PhaseState> next = phase_->OnDyingFinished()) {
    ChangePhase(std::move(next));
  }
}

void Boss::ForceDefeat() {
  if (isDead_ || IsDyingOrDefeated()) {
    return;
  }
  SetHP(0);
  ChangePhase(std::make_unique<DyingState>(*this));
}

void Boss::DestroyParts() {
  for (BossBit *bit : activeBits_) {
    bit->SetOnDestroyedCallback(nullptr);
    bit->Destroy();
  }
  activeBits_.clear();
  for (BossWeakPoint *wp : activeWeakPoints_) {
    wp->SetOnDestroyedCallback(nullptr);
    wp->Destroy();
  }
  activeWeakPoints_.clear();
}

void Boss::Draw3D() {
  if (GetModel()) {
    GetModel()->Draw();
  }
}

void Boss::Draw2D() {
  if (!isUIInitialized_ || isDead_)
    return;

  Transform defaultUv;
  defaultUv.scale = {1.0f, 1.0f, 1.0f};
  defaultUv.rotate = {0.0f, 0.0f, 0.0f};
  defaultUv.translate = {0.0f, 0.0f, 0.0f};

  // 突進予兆時の警告UI描画
  if (action_->ShowsDangerWarning()) {
      // 点滅させる
      float alpha = (std::sin(stateTimer_ * 20.0f) + 1.0f) * 0.5f * 0.8f;
      dangerUI_->SetColor({1.0f, 0.0f, 0.0f, alpha});
      dangerUI_->Update(defaultUv);
      dangerUI_->Draw();
  }

  // ミサイル予兆中のレティクル描画 (Phase2の場合は常にミサイル)
  if (action_->ShowsMissileReticle() && GetPlayer() && GetCamera()) {
    float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
    
    Vector3 playerPos = GetPlayer()->GetTransform().translate;
    Vector2 screenPos = WorldToScreen(playerPos, GetCamera()->GetViewProjectionMatrix(), 1280.0f, 720.0f);

    // 画面外を描画から除外
    if (screenPos.x > -100 && screenPos.x < 1380 && screenPos.y > -100 && screenPos.y < 820) {
      
      // チャージ比率 (0.0~1.0)
      float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
      bool isLocked = stateTimer_ >= 1.0f; // 1.0秒以降はロック完了状態

      Transform uvTransform;
      uvTransform.scale = {1.0f, 1.0f, 1.0f};
      uvTransform.rotate = {0.0f, 0.0f, 0.0f};
      uvTransform.translate = {0.0f, 0.0f, 0.0f};
      
      //  外枠 
      // 外枠は縮小せず、画面中央を中心に公転・自転する。ターゲットを囲む四角い枠（□）にする
      float outerOffset = 160.0f;
      float outerThick = 6.0f;
      float outerLen = 100.0f;
      // 1.0秒でピッタリ90度回転し、四角い枠のままカチッと止まる
      float outerRot = stateTimer_ * (std::numbers::pi_v<float> / 2.0f);
      float finalOuterRot = isLocked ? (std::numbers::pi_v<float> / 2.0f) : outerRot;

      constexpr float baseAngles[4] = {
          0.0f,                               // 上
          std::numbers::pi_v<float>,          // 下
          -std::numbers::pi_v<float> / 2.0f,  // 左
          std::numbers::pi_v<float> / 2.0f    // 右
      };

      for (int i=0; i<4; ++i) {
         float currentAngle = baseAngles[i] + finalOuterRot;
         float ox = sinf(currentAngle) * outerOffset;
         float oy = -cosf(currentAngle) * outerOffset;
         
         reticleSprites_[i]->SetPosition({screenPos.x + ox, screenPos.y + oy});
         // 外枠は「横線（-）」をベースにして回転させることで四角い枠（□）を形成する
         reticleSprites_[i]->SetSize({outerLen, outerThick}); 
         reticleSprites_[i]->SetRotation(currentAngle);
         
         reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, isLocked ? 1.0f : 0.6f});
         reticleSprites_[i]->Update(uvTransform);
         reticleSprites_[i]->Draw();
      }

      //内枠
      // チャージが進むにつれて枠が激しく縮小する。内枠はターゲットを狙う十字（＋）にする
      float innerOffset = 200.0f - (150.0f * chargeRatio);
      float innerThick = 10.0f;
      float innerLen = 50.0f;
      // 1.0秒でピッタリ180度回転する
      float innerRot = -stateTimer_ * std::numbers::pi_v<float>;
      float finalInnerRot = isLocked ? -std::numbers::pi_v<float> : innerRot;

      float innerAlpha = 1.0f;
      if (!isLocked && chargeRatio > 0.7f) {
         innerAlpha = fmodf(stateTimer_ * 15.0f, 1.0f) > 0.5f ? 1.0f : 0.2f;
      }
      
      for (int i=4; i<8; ++i) {
         int idx = i - 4;
         float currentAngle = baseAngles[idx] + finalInnerRot;
         float ox = sinf(currentAngle) * innerOffset;
         float oy = -cosf(currentAngle) * innerOffset;
         
         reticleSprites_[i]->SetPosition({screenPos.x + ox, screenPos.y + oy});
         // 内枠は「縦線（|）」をベースにして回転させることで十字（＋）を形成する
         reticleSprites_[i]->SetSize({innerThick, innerLen}); 
         reticleSprites_[i]->SetRotation(currentAngle);
         
         reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, innerAlpha});
         reticleSprites_[i]->Update(uvTransform);
         reticleSprites_[i]->Draw();
      }

      // センタードット
      if (isLocked) {
         reticleSprites_[8]->SetPosition({screenPos.x, screenPos.y});
         reticleSprites_[8]->SetSize({14.0f, 14.0f}); 
         reticleSprites_[8]->SetRotation(0.0f);
         
         reticleSprites_[8]->SetScale({1.0f, 1.0f});
         reticleSprites_[8]->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
         reticleSprites_[8]->Update(uvTransform);
         reticleSprites_[8]->Draw();
      }
    }
  }
}

void Boss::TakeDamage(int damage, bool isSelfDestruct) {
  // 形態変化中、死亡演出中、または死亡済みの場合はダメージを受け付けない（演出リセット防止）
  if (isDead_ || !phase_->AcceptsDamage())
    return;

  // 装甲が1つでも残っている間はボス本体へのダメージを完全に無効化する
  if (!activeBits_.empty())
    return;

  ApplyDamage(damage);

  // 次の形態へ移るかは今の形態が決める（HP がなくなったら、即消滅ではなく撃破の間へ）
  if (GetHP() <= 0) {
    SetHP(0);
  }
  if (std::unique_ptr<PhaseState> next = phase_->OnDamaged()) {
    ChangePhase(std::move(next));
  }
}

bool Boss::IsDyingOrDefeated() const { return phase_->IsDyingOrDefeated(); }

bool Boss::IsTransitioning() const { return phase_->IsTransitioning(); }

float Boss::GetTransitionProgress() const {
  if (!phase_->IsTransitioning()) {
    return 0.0f;
  }
  return (std::min)(stateTimer_ / kTransitionSeconds_, 1.0f);
}

void Boss::ChangePhase(std::unique_ptr<PhaseState> next) {
  phase_ = std::move(next);
  phase_->Enter();
}

void Boss::UpdateAction() {
  stateTimer_ += kFrameSeconds_;
  if (std::unique_ptr<ActionState> next = action_->Update()) {
    ChangeAction(std::move(next));
  }
}

void Boss::ChangeAction(std::unique_ptr<ActionState> next) {
  action_ = std::move(next);
  stateTimer_ = 0.0f;
  action_->Enter();
}

bool Boss::IsDashing() const { return action_->IsDashing(); }

void Boss::EnterTransition() {
  Logger::Log("Boss entering Phase Transition!\n");
  
  // 形態変化に入った瞬間に、画面上のすべての敵弾を消去する
  ActorManager::GetInstance()->ClearActorsIf([](BaseActor* actor) {
      auto* cleared = dynamic_cast<IClearedByBoss*>(actor);
      if (!cleared) {
          return false;
      }
      cleared->OnBossTransition();
      return true;
  });
  
  stateTimer_ = 0.0f;
  cameraEvents_.push_back(CameraEvent::TransitionStarted);
  startPos_ = transform_.translate;
  if (GetCamera()) {
    Vector3 cPos = GetCamera()->GetRailPosition();
    Vector3 cUp = GetCamera()->GetRailUp();
    Vector3 cForward = GetCamera()->GetRailForward();
    // 画面奥深くへ退避
    targetPos_ = cPos + cForward * 200.0f;
  } else {
    targetPos_ = startPos_;
  }
}

void Boss::EnterPhase2() {
  Logger::Log("Boss entering Phase 2!\n");
  // パターン変化の初期化など
  ChangeAction(std::make_unique<Phase2CooldownAction>(*this)); // 形態変化直後はスキを作る（カメラ引き戻し用）
  attackStep_ = -1; // -1から始めることで、次の行動がミサイル（0 % 2 == 0）になる
  startPos_ = transform_.translate;
  targetPos_ = transform_.translate;
}

void Boss::EnterDying() {
  Logger::Log("Boss entering Dying phase!\n");
  
  // ボス撃破と同時に画面上のすべての敵弾（ミサイル含む）を誘爆させる
  ActorManager::GetInstance()->ClearActorsIf([](BaseActor* actor) {
      auto* cleared = dynamic_cast<IClearedByBoss*>(actor);
      if (!cleared) {
          return false;
      }
      cleared->OnBossDefeated();
      return true; // 誘爆させつつリストからも直ちに消去する
  });

  // 残っているコアも爆発させて消す
  for (const ActorRef<BossCore>& coreRef : cores_) {
      if (BossCore* core = coreRef.Get()) {
          EffectManager::GetInstance()->PlayEnemyDeathSimpleEffect(core->GetTransform().translate, kCoreBlastColor_);
          core->Destroy();
      }
  }
  cores_.clear();

  // 残った装甲と突進の的を消す（ボスより長く残って、消えたボスを指さないようにする）
  DestroyParts();

  // コライダーを無効化（メモリは破棄しない）
  SetColliderEnabled(false);
  dyingFlashAmount_ = 0.0f;
  // 突進の途中でも、突進の処理をやめて撃破の処理へ移す
  ChangeAction(std::make_unique<IdleAction>(*this));
  // HPバーを非表示にする
  isUIInitialized_ = false;
  if (onDyingStartedCallback_) {
    onDyingStartedCallback_(transform_.translate);
  }
}

void Boss::EnterDefeated() {
  Logger::Log("Boss Defeated!\n");

  NotifyDestroyed(false);
  Destroy();
}

void Boss::PlaceAtSpawn() {
  // カメラの前方（基準位置）に配置
  if (GetCamera()) {
    Vector3 cPos = GetCamera()->GetRailPosition();
    Vector3 cRight = GetCamera()->GetRailRight();
    Vector3 cUp = GetCamera()->GetRailUp();
    Vector3 cForward = GetCamera()->GetRailForward();
    Vector3 center = cPos + cRight * GetSpawnOffset().x + cUp * GetSpawnOffset().y +
                     cForward * GetSpawnOffset().z;
    transform_.translate = center;
    startPos_ = center;
    targetPos_ = center;
    hoverWaypoints_ = {center, center, center,
                       center}; // 初回は同じ位置に留まる
  }
}

bool Boss::UpdatePhase1Hover() {
  float duration = 2.5f; // 飛行にかける時間
  float t = std::min(stateTimer_ / duration, 1.0f);

  // SmoothStepによる緩急
  float easeT = t * t * (3.0f - 2.0f * t);

  // スプライン曲線による移動
  transform_.translate =
      CatmullRom(hoverWaypoints_[0], hoverWaypoints_[1], hoverWaypoints_[2],
                 hoverWaypoints_[3], easeT);

  // 常にプレイヤーの方向を向く
  if (GetPlayer()) {
    Vector3 pPos = GetPlayer()->GetTransform().translate;
    Vector3 dir = {pPos.x - transform_.translate.x,
                   pPos.y - transform_.translate.y,
                   pPos.z - transform_.translate.z};
    transform_.rotate.y = std::atan2(dir.x, dir.z) + std::numbers::pi_v<float>;
  }

  // 移動完了で予兆ステートへ
  if (stateTimer_ >= duration) {
    return true;
  }
  return false;
}

bool Boss::UpdatePhase1Telegraph() {
  // 予兆：小刻みに震える
  float shakeAmount = 0.2f;
  float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  transform_.translate.x = targetPos_.x + rx;
  transform_.translate.y = targetPos_.y + ry;
  transform_.translate.z = targetPos_.z + rz;

  // 予兆パーティクルエフェクトの発生（チャージの進行度 0.0 ~ 1.0 を渡す）
  float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
  Vector3 effectPos = transform_.translate;
  if (GetPlayer()) {
      effectPos = GetPlayer()->GetTransform().translate;
  }
  EffectManager::GetInstance()->PlayBossTelegraphEffect(transform_.translate, effectPos, chargeRatio, attackPattern_);

  // 1秒経過でロックオン完了。ミサイルの場合はアニメーション後さらに1秒間（計2.0秒まで）待ってから発射する
  float telegraphDuration = (attackPattern_ == 1) ? 2.0f : 1.0f;
  if (stateTimer_ >= telegraphDuration) {
    // 震えを元に戻す
    transform_.translate = targetPos_;
    return true;
  }
  return false;
}

void Boss::FirePhase1Barrage() {
  EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);

  if (GetPlayer()) {
    Vector3 playerPos = GetPlayer()->GetTransform().translate;
    Vector3 myPos = transform_.translate;
    Vector3 dir = {playerPos.x - myPos.x, playerPos.y - myPos.y,
                   playerPos.z - myPos.z};
    float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dist > 0.001f) {
      dir.x /= dist;
      dir.y /= dist;
      dir.z /= dist;

      auto spawnBullet = [&](const Vector3 &vel, EnemyBulletType type,
                             int waitFrames = 0) {
        auto bullet = std::make_unique<EnemyBullet>();
        bullet->Initialize(
            PrefabManager::GetInstance()->GetObject3dRenderer(), myPos, vel,
            GetPlayer(), type);
        if (type == EnemyBulletType::LockOnDestructible) {
          bullet->SetSwarmWait(waitFrames);
        }
        ActorManager::GetInstance()->AddActor(std::move(bullet));
      };

      if (attackPattern_ == 1) {
        // ミサイル
        for (int i = 0; i < 4; ++i) {
          Vector3 rightDir = {dir.z, 0.0f, -dir.x};
          Vector3 backwardDir = {-dir.x, 0.0f, -dir.z};
          float bLen = std::sqrt(backwardDir.x * backwardDir.x +
                                 backwardDir.z * backwardDir.z);
          if (bLen > 0.001f) {
            backwardDir.x /= bLen;
            backwardDir.z /= bLen;
          }

          float spread = (i == 0)   ? -1.0f
                         : (i == 1) ? -0.4f
                         : (i == 2) ? 0.4f
                                    : 1.0f;
          float upwardForce = ((rand() % 100) / 100.0f) * 0.5f;
          float launchSpeed =
              15.0f; // 横への展開速度を少しだけ落とす（18.0 -> 15.0）
          float backwardSpeed =
              0.0f; // 後ろには飛ばさず、ボスの真横に展開する
          int waitFrames =
              40 + (i * 50); // 一発ごとの間隔を 30f から 50f に延長
          Vector3 vel = {rightDir.x * spread * launchSpeed +
                             backwardDir.x * backwardSpeed,
                         upwardForce,
                         rightDir.z * spread * launchSpeed +
                             backwardDir.z * backwardSpeed};
          spawnBullet(vel, EnemyBulletType::LockOnDestructible, waitFrames);
        }
      } else {
        // 通常弾（扇状）
        float normalSpeed = 0.5f;
        for (int i = 0; i < 5; ++i) {
          float angle = (-20.0f + i * 10.0f) * (std::numbers::pi_v<float> / 180.0f);
          Vector3 vel = {
              (dir.x * std::cos(angle) + dir.z * std::sin(angle)) *
                  normalSpeed,
              dir.y * normalSpeed,
              (-dir.x * std::sin(angle) + dir.z * std::cos(angle)) *
                  normalSpeed};
          spawnBullet(vel, EnemyBulletType::NormalDestructible);
        }
      }

      cameraEvents_.push_back(CameraEvent::BarrageFired);
    }
  }

}

bool Boss::UpdatePhase1Cooldown() {
  // 2秒経過したら再びHoverに戻ってループ
  if (stateTimer_ >= 2.0f) {
    startPos_ = transform_.translate;

    // 次の攻撃パターンを切り替える（ランダム抽選）
    // 0:通常弾, 1:ミサイル, 2:突進
    std::vector<int> availablePatterns = {0, 1};
    
    if (dashCooldown_ > 0) {
        dashCooldown_--;
    } else {
        // クールダウンが明けていれば突進(2)を抽選候補に加える
        availablePatterns.push_back(2);
    }
    
    int randomIndex = rand() % availablePatterns.size();
    attackPattern_ = availablePatterns[randomIndex];
    
    if (attackPattern_ == 2) {
        dashCooldown_ = 3; // 突進を選んだら3ターンのクールダウンを設定
        return true; // Hoverへは行かずに突進予兆へ遷移
    }

    if (GetCamera()) {
      Vector3 cPos = GetCamera()->GetRailPosition();
      Vector3 cRight = GetCamera()->GetRailRight();
      Vector3 cUp = GetCamera()->GetRailUp();
      Vector3 cForward = GetCamera()->GetRailForward();

      // 1. 攻撃パターンに基づく目標座標（着地地点）の決定 = 予兆（Telegraph）
      if (attackPattern_ == 1) {
        // ミサイル：画面の奥深く（遠距離）へ
        targetPos_ = cPos + cUp * 15.0f + cForward * 250.0f;
      } else {
        // 通常弾：プレイヤーの近く（近距離）へ
        targetPos_ = cPos + cUp * 5.0f + cForward * 110.0f;
      }

      // 少しランダムにズラす
      float offsetX =
          ((rand() % 100) / 100.0f - 0.5f) * 40.0f; // ズレ幅も少し抑える
      targetPos_.x += offsetX;

      // 2. スプライン曲線の制御点を生成（行き方はランダム）
      hoverWaypoints_[1] = startPos_;
      hoverWaypoints_[2] = targetPos_;

      // 引っ張り点（P0, P3）をランダムに配置して有機的なカーブを作る
      int flightPattern = rand() % 3;
      if (flightPattern == 0) {
        // 左から大きく回り込む
        hoverWaypoints_[0] = cPos - cRight * 600.0f + cUp * 50.0f;
        hoverWaypoints_[3] = cPos + cRight * 600.0f - cUp * 50.0f;
      } else if (flightPattern == 1) {
        // 上空へ大きく膨らむ
        hoverWaypoints_[0] = cPos + cUp * 600.0f + cForward * 100.0f;
        hoverWaypoints_[3] = cPos - cUp * 600.0f + cForward * 100.0f;
      } else {
        // 右から大きく回り込む
        hoverWaypoints_[0] = cPos + cRight * 600.0f + cUp * 50.0f;
        hoverWaypoints_[3] = cPos - cRight * 600.0f - cUp * 50.0f;
      }
    } else {
      targetPos_ = startPos_;
      hoverWaypoints_ = {startPos_, startPos_, startPos_, startPos_};
    }

    return true;
  }
  return false;
}

bool Boss::UpdatePhase2Hover() {
  // 高速・広範囲なイージング移動
  float t = std::min(stateTimer_ / 1.5f, 1.0f);
  float easeT = 1.0f - std::pow(1.0f - t, 3.0f);

  transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeT;
  transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeT;
  transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeT;

  if (GetPlayer()) {
    Vector3 pPos = GetPlayer()->GetTransform().translate;
    Vector3 dir = {pPos.x - transform_.translate.x,
                   pPos.y - transform_.translate.y,
                   pPos.z - transform_.translate.z};
    transform_.rotate.y = std::atan2(dir.x, dir.z) + std::numbers::pi_v<float>;
  }

  return stateTimer_ >= 1.5f;
}

bool Boss::UpdatePhase2Telegraph() {
  // 激しい予兆（ミサイル用）
  float shakeAmount = 0.4f;
  float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
  transform_.translate.x = targetPos_.x + rx;
  transform_.translate.y = targetPos_.y + ry;
  transform_.translate.z = targetPos_.z + rz;

  // 予兆パーティクルエフェクトの発生（チャージの進行度 0.0 ~ 1.0 を渡す）
  float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
  Vector3 effectPos = transform_.translate;
  if (GetPlayer()) {
      effectPos = GetPlayer()->GetTransform().translate;
  }
  EffectManager::GetInstance()->PlayBossTelegraphEffect(transform_.translate, effectPos, chargeRatio, 1); // Phase 2のTelegraphはミサイル固定なので1

  if (stateTimer_ >= 1.0f) {
    transform_.translate = targetPos_;
    return true;
  }
  return false;
}

void Boss::FirePhase2Missiles() {
  // 発射の瞬間に発散エフェクト（大爆発＋ショックウェーブ）を発生
  EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);

  if (GetPlayer()) {
    Vector3 playerPos = GetPlayer()->GetTransform().translate;
    Vector3 myPos = transform_.translate;
    Vector3 dir = {playerPos.x - myPos.x, playerPos.y - myPos.y,
                   playerPos.z - myPos.z};
    float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dist > 0.001f) {
      dir.x /= dist;
      dir.y /= dist;
      dir.z /= dist;
      auto spawnBullet = [&](const Vector3 &vel, EnemyBulletType type,
                             int waitFrames) {
        auto bullet = std::make_unique<EnemyBullet>();
        bullet->Initialize(
            PrefabManager::GetInstance()->GetObject3dRenderer(), myPos, vel,
            GetPlayer(), type);
        bullet->SetSwarmWait(
            waitFrames); // ランダムではなく指定された待機時間をセット
        ActorManager::GetInstance()->AddActor(std::move(bullet));
      };

      // スウォーム弾幕（6発）をボスの周囲に円形展開（12時方向から時計回りに順次発射）
      constexpr int kMissileCount = 6;
      float deploySpeedX = 7.5f; 
      float deploySpeedY = 3.8f; 
      Vector3 rightDir = {dir.z, 0.0f, -dir.x};
      Vector3 upDir = {0.0f, 1.0f, 0.0f};
      if (GetCamera()) {
        upDir = GetCamera()->GetUp();
      }

      Vector3 backwardDir = {-dir.x, 0.0f, -dir.z};
      float bLen = std::sqrt(backwardDir.x * backwardDir.x +
                             backwardDir.z * backwardDir.z);
      if (bLen > 0.001f) {
        backwardDir.x /= bLen;
        backwardDir.z /= bLen;
      }

      float backwardSpeed = 6.0f; // 本来の奥方向（ボスの背後）への展開速度

      for (int i = 0; i < kMissileCount; ++i) {
        // 12時方向（真上）を基準に、時計回りに60度ずつ配置
        // i=0: 上, i=1: 右上, i=2: 右下, i=3: 真下, i=4: 左下, i=5: 左上
        float angle = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(kMissileCount));
        float sinAngle = std::sin(angle);
        float cosAngle = std::cos(angle);

        // 12時から時計回りに順番に発射
        int waitFrames = 80 + (i * 20);

        Vector3 vel = {
            rightDir.x * (sinAngle * deploySpeedX) + upDir.x * (cosAngle * deploySpeedY) + backwardDir.x * backwardSpeed,
            rightDir.y * (sinAngle * deploySpeedX) + upDir.y * (cosAngle * deploySpeedY) + backwardDir.y * backwardSpeed,
            rightDir.z * (sinAngle * deploySpeedX) + upDir.z * (cosAngle * deploySpeedY) + backwardDir.z * backwardSpeed};

        spawnBullet(vel, EnemyBulletType::LockOnDestructible, waitFrames);
      }

      cameraEvents_.push_back(CameraEvent::MissilesDeployed);
    }
  }
}

bool Boss::UpdatePhase2Cooldown() {
  float waitTime = (attackStep_ == -1) ? 2.0f : 1.0f; // 形態変化直後は演出用に長め（2.0秒）に待つ
  if (stateTimer_ >= waitTime) {
    attackStep_++;
    
    // ランダム抽選ロジック
    std::vector<int> availablePatterns = {1}; // 1: ミサイル
    if (dashCooldown_ > 0) {
        dashCooldown_--;
    } else {
        availablePatterns.push_back(2); // 2: 突進
    }
    int randomIndex = rand() % availablePatterns.size();
    attackPattern_ = availablePatterns[randomIndex];

    if (attackPattern_ == 2) {
        dashCooldown_ = 3;
    }
    
    startPos_ = targetPos_;
    if (GetCamera()) {
      Vector3 cPos = GetCamera()->GetRailPosition();
      Vector3 cRight = GetCamera()->GetRailRight();
      Vector3 cUp = GetCamera()->GetRailUp();
      Vector3 cForward = GetCamera()->GetRailForward();
      // 次の攻撃パターンに応じて目標位置を変える
      if (attackPattern_ == 1) {
        // 次はミサイル攻撃：画面奥深く（突進と同じ距離200）へ移動
        targetPos_ = cPos + cForward * 200.0f;
        float offsetX = ((rand() % 100) / 100.0f - 0.5f) * 40.0f;
        targetPos_.x += offsetX;
      } else if (attackPattern_ == 2) {
        // 次は突進攻撃：一旦手前（基準位置付近）へ移動し、その後大きく下がる演出に繋げる
        Vector3 center = cPos + cRight * GetSpawnOffset().x + cUp * GetSpawnOffset().y +
                         cForward * GetSpawnOffset().z;
        float offsetX = ((rand() % 100) / 100.0f - 0.5f) * 24.0f;
        float offsetY = ((rand() % 100) / 100.0f - 0.5f) * 12.0f;
        targetPos_.x = center.x + offsetX;
        targetPos_.y = center.y + offsetY;
        targetPos_.z = center.z;
      }
    }
    return true;
  }
  return false;
}

bool Boss::UpdateDashTelegraph() {
  // 初回のみ：装甲退避と的のスポーン
  if (stateTimer_ <= 0.02f) {
      SoundManager::GetInstance()->PlaySE("boss_charge");
      for (auto* bit : activeBits_) {
          bit->SpreadOut();
      }
      Vector3 wpOffsets[4] = {
          { 0.0f,  1.8f, -1.0f},
          { 0.0f, -1.8f, -1.0f},
          {-1.8f,  0.0f, -1.0f},
          { 1.8f,  0.0f, -1.0f}
      };
      for (int i = 0; i < 4; ++i) {
          auto wp = std::make_unique<BossWeakPoint>();
          auto model = std::make_unique<Object3d>();
          model->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
          model->SetModel("__builtin_crystal");
          model->SetColor({1.0f, 0.2f, 0.2f, 1.0f});
          wp->SetModel(std::move(model));
          wp->SetBoss(this);
          wp->SetOffset(wpOffsets[i]);
          wp->SetId(i);
          wp->GetTransform().scale = {3.0f, 3.0f, 3.0f};
          
          BossWeakPoint* wpPtr = wp.get();
          wp->SetOnDestroyedCallback([this, wpPtr](bool) { this->OnWeakPointDestroyed(wpPtr); });
          
          activeWeakPoints_.push_back(wpPtr);
          ActorManager::GetInstance()->AddActor(std::move(wp));
      }
      
      // Z奥へ下がるための目標位置を設定
      startPos_ = transform_.translate;
      if (GetCamera()) {
          Vector3 cPos = GetCamera()->GetRailPosition();
          Vector3 cForward = GetCamera()->GetRailForward();
          targetPos_ = cPos + cForward * 200.0f; // かなり奥へ
      }
  }

  // 最初の0.5秒で奥へ下がる
  float moveT = std::min(stateTimer_ / 0.5f, 1.0f);
  float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f);
  transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeMoveT;
  transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeMoveT;
  transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeMoveT;

  // 奥に到達してから大きく震える
  if (moveT >= 1.0f) {
      float shakeAmount = 0.6f;
      float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
      float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
      float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
      transform_.translate.x = targetPos_.x + rx;
      transform_.translate.y = targetPos_.y + ry;
      transform_.translate.z = targetPos_.z + rz;
  }

  // 突進予兆パーティクルエフェクトの発生
  float totalTelegraphTime = 8.0f;

  if (stateTimer_ >= totalTelegraphTime) {
    transform_.translate = targetPos_;
    startPos_ = targetPos_;
    
    // 突進の目標座標を「プレイヤーの現在の位置のさらに後方」へ設定
    if (GetPlayer() && GetCamera()) {
        Vector3 pPos = GetPlayer()->GetTransform().translate;
        Vector3 cForward = GetCamera()->GetRailForward();
        targetPos_ = pPos - cForward * 50.0f; // 自機の背面へ通り抜ける
    } else if (GetCamera()) {
        Vector3 cPos = GetCamera()->GetRailPosition();
        Vector3 cForward = GetCamera()->GetRailForward();
        targetPos_ = cPos - cForward * 50.0f; // カメラの背面へ通り抜ける
    } else {
        targetPos_.z -= 200.0f;
    }

    cameraEvents_.push_back(CameraEvent::DashStarted);

    return true;
  }
  return false;
}

bool Boss::UpdateDash() {
  // 0.8秒で画面を通り抜ける超高速移動
  float t = std::min(stateTimer_ / 0.8f, 1.0f);
  float easeT = t * t; // EaseIn

  transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeT;
  transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeT;
  transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeT;

  // 途中でプレイヤーとの衝突判定は OnCollision で行われる。
  // (Player側で被弾処理が行われる)

  if (stateTimer_ >= 0.8f) {
    // 突進終了（カウンター失敗で通り抜けた後）
    // 残った的を安全に消す
    for (auto* wp : activeWeakPoints_) {
        wp->SetOnDestroyedCallback(nullptr); // コールバックを解除
        wp->Destroy(); // 即死させる
    }
    activeWeakPoints_.clear();
    // 装甲を元に戻す
    for (auto* bit : activeBits_) {
        bit->ResetPosition();
    }

    // カメラ後方から再配置し、Cooldownへ
    return true;
  }
  return false;
}

bool Boss::UpdateStagger() {
    // カウンター成功時、大きく後方へ弾き飛ばされる
    if (stateTimer_ <= 0.02f) {
        startPos_ = transform_.translate;
        if (GetCamera()) {
            Vector3 cForward = GetCamera()->GetRailForward();
            // 奥へ吹っ飛ぶ目標 + 少し下に落ちる
            targetPos_ = startPos_ + cForward * 100.0f;
            targetPos_.y -= 25.0f;
        }
    }

    // ヒットストップのため最初の0.3秒間は止まる
    if (stateTimer_ < 0.3f) {
        // ボス自身も激しく振動させる（ブレ）
        float shake = 0.2f;
        transform_.rotate.z += ((rand() % 100) / 100.0f - 0.5f) * shake;
        transform_.rotate.x += ((rand() % 100) / 100.0f - 0.5f) * shake;
    } else if (stateTimer_ < 1.3f) {
        // 0.3〜1.3秒で、奥・下へノックバック
        float moveT = (stateTimer_ - 0.3f) / 1.0f;
        float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f); // EaseOut
        transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeMoveT;
        transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeMoveT;
        transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeMoveT;
    } else {
        // 1.3〜2.0秒で、元の位置(startPos_)へ復帰
        float moveT = (stateTimer_ - 1.3f) / 0.7f;
        float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f); // EaseOut
        transform_.translate.x = targetPos_.x + (startPos_.x - targetPos_.x) * easeMoveT;
        transform_.translate.y = targetPos_.y + (startPos_.y - targetPos_.y) * easeMoveT;
        transform_.translate.z = targetPos_.z + (startPos_.z - targetPos_.z) * easeMoveT;
    }

    // 姿勢を崩す（仰け反り＋横に傾く）
    // 0.3秒〜1.3秒で最大まで傾き、1.3秒〜2.0秒で元に戻る
    float tiltT = 0.0f;
    if (stateTimer_ < 1.3f) {
        tiltT = (stateTimer_ - 0.3f) / 1.0f; 
        if (tiltT < 0.0f) tiltT = 0.0f;
        tiltT = std::sin(tiltT * 3.141592f / 2.0f); // EaseOut
    } else {
        // 姿勢もEaseOutで戻るように移動と同じ計算を使用する
        float moveT = (stateTimer_ - 1.3f) / 0.7f;
        float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f);
        tiltT = 1.0f - easeMoveT;
        if (tiltT < 0.0f) tiltT = 0.0f;
    }
        
    // 後ろへ仰け反り（X回転）と、力なく横に倒れる（Z回転）
    transform_.rotate.x = tiltT * -1.2f; // 上を向くように仰け反る
    transform_.rotate.z = tiltT * 0.8f;  // 横に傾く

    if (stateTimer_ >= 2.0f) {
        return true;
    }
  return false;
}
