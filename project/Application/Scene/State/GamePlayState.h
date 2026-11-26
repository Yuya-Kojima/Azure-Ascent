#pragma once
#include "Math/Vector3.h"
#include <memory>

class GamePlayScene;
class ICamera;

/// <summary>
/// ボスへの最後の一撃が入ったときのボスの様子
/// </summary>
struct BossLastHit {
  Vector3 position; // ボスの位置
  float size = 0.0f; // ボスの大きさ
  Vector3 rotation;  // ボスの向き（オイラー角）
};

/// <summary>
/// ゲームの進行の状態の基底クラス。入ったときの準備・次の状態・その状態での世界の進め方と描き方を決める
/// （既定の答えは操作中のもの）
/// </summary>
class GamePlayState {
public:
  explicit GamePlayState(GamePlayScene &scene) : scene_(scene) {}
  virtual ~GamePlayState() = default;

  //=======================
  // 状態の移り変わり
  //=======================

  /// <summary>
  /// この状態に入ったときの準備
  /// </summary>
  virtual void Enter() {}

  /// <summary>
  /// 毎フレームの更新。次の状態へ移るなら、その状態を返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<GamePlayState> Update() { return nullptr; }

  /// <summary>
  /// ボスへの最後の一撃が入った。次の状態へ移るなら、その状態を返す（このままなら nullptr）
  /// </summary>
  virtual std::unique_ptr<GamePlayState> OnBossLastHit(const BossLastHit &) {
    return nullptr;
  }

  //=======================
  // 世界の進め方
  //=======================

  /// <summary>
  /// レール・敵・弾・当たり判定を進めるか
  /// </summary>
  virtual bool UpdatesWorld() const { return true; }

  /// <summary>
  /// 世界の時間の速さ（1 で普通、1 より小さいとゆっくり）
  /// </summary>
  virtual float GetWorldTimeScale() const { return 1.0f; }

  /// <summary>
  /// 世界が止まっていても、レールを動かすか
  /// </summary>
  virtual bool MovesRailWhileWorldStopped() const { return false; }

  /// <summary>
  /// レールカメラの向きを、操作開始時の自機の位置へ固定しておくか
  /// </summary>
  virtual bool HoldsRailCameraAim() const { return false; }

  /// <summary>
  /// この状態の演出を進め、演出のカメラを返す（演出がなければ nullptr）
  /// </summary>
  /// <param name="canMove">演出を進めてよいか（Play モードで、止めていない）</param>
  virtual const ICamera *UpdateSequence(bool canMove) {
    (void)canMove;
    return nullptr;
  }

  //=======================
  // 自機
  //=======================

  /// <summary>
  /// 自機の操作を受けるか
  /// </summary>
  virtual bool AcceptsPlayerControl() const { return true; }

  /// <summary>
  /// 自機の位置・向きを演出が決めているか
  /// </summary>
  virtual bool IsPlayerPosedBySequence() const { return false; }

  /// <summary>
  /// 羽ばたきを進めるか
  /// </summary>
  /// <param name="isWorldUpdated">このフレームに世界を進めたか</param>
  /// <param name="isSequenceMoving">演出を進めてよいか（Play モードで、エディタで止めていない）</param>
  virtual bool MovesWing(bool isWorldUpdated, bool isSequenceMoving) const {
    (void)isSequenceMoving;
    return isWorldUpdated;
  }

  /// <summary>
  /// ポーズ画面を開けるか
  /// </summary>
  virtual bool CanPause() const { return false; }

  /// <summary>
  /// デバッグのボタンで、自機を倒す・クリアを始めることができるか
  /// </summary>
  virtual bool CanEndByDebug() const { return false; }

  //=======================
  // 画面の表示
  //=======================

  /// <summary>
  /// HUD の濃さ
  /// </summary>
  /// <param name="playAlpha">操作中の HUD の濃さ（開始演出のあとに現れる途中の値）</param>
  virtual float GetHudAlpha(float playAlpha) const { return playAlpha; }

  /// <summary>
  /// 照準・敵の UI・迎撃の印を出すか
  /// </summary>
  virtual bool ShowsAimUI() const { return false; }

  /// <summary>
  /// スプライトの UI を出すか
  /// </summary>
  virtual bool ShowsUI() const { return true; }

  /// <summary>
  /// HUD を出すか
  /// </summary>
  virtual bool ShowsHud() const { return true; }

  /// <summary>
  /// HUD の上、クリア演出の帯と目標カードの下に描く
  /// </summary>
  virtual void DrawBehindCards() {}

  /// <summary>
  /// クリア演出の帯と目標カードより手前に描く
  /// </summary>
  virtual void DrawInFrontOfCards() {}

protected:
  // 状態を持っているシーン
  GamePlayScene &GetScene() const { return scene_; }

private:
  GamePlayScene &scene_;
};
