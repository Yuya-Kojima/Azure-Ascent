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
  bool IsDashing() const;

  // 自機に当たっても自爆しない
  void OnHitPlayer() override {}

  // 画面の外でカメラの後ろに取り残されても消えない
  bool CanDespawnBehindCamera() const override { return false; }

  // 死亡演出中または死亡済みかどうかの判定
  bool IsDyingOrDefeated() const;

  // 形態変化中かどうかの判定
  bool IsTransitioning() const;

  bool HasActiveBits() const { return !activeBits_.empty(); }

  bool CanBeLockedOn() const override { return false; }

  // ステータスのゲッター/セッター
  int GetMaxHP() const { return maxHp_; }
  void SetMaxHP(int hp) { maxHp_ = hp; }

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
  // 形態（Boss.cpp で定義する。ボスの入れ子のクラスなので、ボスの private な関数を呼べる）
  class PhaseState;      // 基底
  class Phase1State;     // 第 1 形態
  class TransitionState; // 形態変化
  class Phase2State;     // 第 2 形態
  class DyingState;      // 撃破の間
  class DefeatedState;   // 撃破済み

  // 形態を next に入れ替え、入ったときの準備をさせる
  void ChangePhase(std::unique_ptr<PhaseState> next);
  // 形態変化に入る（敵弾を消し、画面奥へ退く位置を決める）
  void EnterTransition();
  // 第 2 形態に入る（形態変化の直後はスキを作る）
  void EnterPhase2();
  // 撃破の間に入る（敵弾を誘爆させ、コア・装甲・的を消し、当たり判定と HP バーを消す）
  void EnterDying();
  // 撃破済みになる（倒されたことを知らせて消える）
  void EnterDefeated();
  // 形態変化の動き。終わったら true
  bool UpdatePhaseTransition();
  // 撃破の間の動き。撃破の見た目を外から進めていなければ、その場で爆発して true
  bool UpdateDying();
  // 撃破の間の体の色（元の色から赤く光らせる色へ、光らせる割合で寄せる）
  Vector4 GetDyingColor() const;
  // 残っている装甲と突進の的を消す
  void DestroyParts();

  // 動き（Boss.cpp で定義する。ボスの入れ子のクラスなので、ボスの private な関数を呼べる）
  class ActionState;           // 基底
  class IdleAction;            // 何もしない（形態が動きを決めている間）
  class Phase1EnterAction;     // 第 1 形態の登場
  class Phase1HoverAction;     // 第 1 形態の滞空
  class Phase1TelegraphAction; // 第 1 形態の予兆
  class Phase1AttackAction;    // 第 1 形態の攻撃
  class Phase1CooldownAction;  // 第 1 形態のクールダウン
  class Phase2HoverAction;     // 第 2 形態の滞空
  class Phase2TelegraphAction; // 第 2 形態の予兆
  class Phase2AttackAction;    // 第 2 形態の攻撃
  class Phase2CooldownAction;  // 第 2 形態のクールダウン
  class DashTelegraphAction;   // 突進の予兆
  class DashAction;            // 突進
  class StaggerAction;         // ダウン（カウンター成功）
  class DashCooldownAction;    // 突進のあと

  // 今の動きを 1 フレーム進め、次の動きが返ってきたら入れ替える
  void UpdateAction();
  // 動きを next に入れ替え、動きの時間を 0 に戻して、入ったときの準備をさせる
  void ChangeAction(std::unique_ptr<ActionState> next);

  // カメラの前の基準の位置に置く（登場）
  void PlaceAtSpawn();
  // 第 1 形態の滞空。移り終えたら true
  bool UpdatePhase1Hover();
  // 第 1 形態の予兆。溜め終えたら true
  bool UpdatePhase1Telegraph();
  // 第 1 形態の攻撃（扇状の弾かミサイルを撃つ）
  void FirePhase1Barrage();
  // 第 1 形態のクールダウン。待ち終えて次の攻撃を選んだら true（突進でなければ、滞空の道筋も決める）
  bool UpdatePhase1Cooldown();
  // 第 2 形態の滞空。移り終えたら true
  bool UpdatePhase2Hover();
  // 第 2 形態の予兆。溜め終えたら true
  bool UpdatePhase2Telegraph();
  // 第 2 形態の攻撃（ミサイルを円形に出す）
  void FirePhase2Missiles();
  // 第 2 形態のクールダウン。待ち終えて次の攻撃とその位置を決めたら true
  bool UpdatePhase2Cooldown();
  // 突進の予兆。溜め終えて突進の行き先を決めたら true
  bool UpdateDashTelegraph();
  // 突進。通り抜けて的を消したら true
  bool UpdateDash();
  // ダウン。弾き飛ばされて元の位置へ戻ったら true
  bool UpdateStagger();
  // ダウンに入る（カウンター成功の音・装甲を戻す・カメラと光の出来事）
  void BeginStagger();

  int maxHp_ = 100;
  std::unique_ptr<PhaseState> phase_; // 今の形態
  static constexpr float kPhase2HpRatio_ = 0.7f; // HP がこの割合まで減ったら形態変化する
  static constexpr float kTransitionSeconds_ = 3.5f; // 形態変化にかける秒数
  static constexpr Vector4 kCoreBlastColor_ = {1.0f, 0.5f, 0.0f, 1.0f}; // 撃破のときにコアが爆発する色
  std::vector<CameraEvent> cameraEvents_; // まだ受け取られていないカメラの出来事

  std::unique_ptr<ActionState> action_; // 今の動き
  float stateTimer_ = 0.0f;              // 今の動き（形態変化の間は形態変化）に入ってからの秒数
  static constexpr float kFrameSeconds_ = 1.0f / 60.0f;      // 1 フレームの秒数
  static constexpr float kDashCooldownSeconds_ = 1.5f;       // 突進のあとに隙を晒す秒数
  static constexpr int kPatternMissile_ = 1;                 // attackPattern_ のミサイル
  static constexpr int kPatternDash_ = 2;                    // attackPattern_ の突進
  static constexpr Vector4 kTelegraphColor_ = {1.0f, 0.5f, 0.0f, 1.0f};     // 予兆の体の色（オレンジ）
  static constexpr Vector4 kDashTelegraphColor_ = {1.0f, 0.0f, 0.0f, 1.0f}; // 突進の予兆の体の色（赤）
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
