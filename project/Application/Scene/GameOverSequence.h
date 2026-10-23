#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <array>
#include <memory>

class GameCamera;
class ICamera;
class RailCamera;

/// <summary>
/// ゲームオーバーの空中の演出
/// 被弾して時間がゆっくりになる → カメラがレールを離れてドラゴンの横に並ぶ → 翼でもがくが、打つたびに弱くなる
/// → 翼が畳まれて力が抜ける → カメラが上へ引き、ドラゴンが雲海へ沈む → 下りの雲の遷移の合図
/// 位置はすべて、Start の時点のドラゴンの位置と、レールの水平な右・前の向きを基準に求める
/// </summary>
class GameOverSequence {
public:
  GameOverSequence();
  ~GameOverSequence();

  /// <summary>
  /// 演出を始める
  /// </summary>
  /// <param name="railCamera">被弾した瞬間のレールカメラ（最初のカメラの位置と向きに使う）</param>
  /// <param name="dragonPos">被弾した瞬間のドラゴンの位置</param>
  /// <param name="dragonRot">被弾した瞬間のドラゴンの向き</param>
  /// <param name="seaHeight">雲海の面の高さ（ワールドのY）</param>
  void Start(const RailCamera &railCamera, const Vector3 &dragonPos,
             const Vector3 &dragonRot, float seaHeight);

  /// <summary>
  /// 止める（演出の途中で Play をやめたとき）
  /// </summary>
  void Stop();

  /// <summary>
  /// 更新（ドラゴンの位置・向き・翼、カメラ）
  /// </summary>
  /// <param name="isSkipPressed">決定キーが押されたか（押されたら、すぐ雲の遷移の合図を出す）</param>
  void Update(bool isSkipPressed);

  /// <summary>
  /// 演出中か
  /// </summary>
  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 雲の遷移を始める合図（一度だけ true を返す）
  /// </summary>
  bool ConsumeTransitionCue();

  /// <summary>
  /// 左右の翼が今打ち下ろしている強さ（0〜1）を渡す（Update の前に毎フレーム）
  /// 打ち下ろしに合わせて体を浮かせ、首をあおり、左右の差で体を揺さぶる
  /// </summary>
  void SetWingPush(float left, float right) {
    leftPush_ = left;
    rightPush_ = right;
  }

  /// <summary>
  /// 左右の翼の付け根の角度（度）を渡す（一打ちごとの上げきり・下ろしきりを測って ImGui に出す）
  /// </summary>
  void RecordWingAngle(float left, float right);

  const Vector3 &GetDragonPosition() const { return dragonPos_; }
  const Vector3 &GetDragonRotation() const { return dragonRot_; }
  const DragonWingAnimator::State &GetWingState() const { return wingState_; }
  const DragonWingAnimator::Gesture &GetGesture() const { return gesture_; }

  /// <summary>
  /// 世界（敵・弾・翼）の時間の速さ（1 で普通の速さ）
  /// </summary>
  float GetTimeScale() const;

  /// <summary>
  /// HUD の不透明度（被弾から消えていく）
  /// </summary>
  float GetHudAlpha() const;

  /// <summary>
  /// 演出のカメラ
  /// </summary>
  const ICamera *GetCamera() const;

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

private:
  // もがく一打ち（time から次の一打ちまで、この翼の状態で打つ）
  struct StruggleBeat {
    float time = 0.0f;               // この一打ちを始める時刻（秒）
    DragonWingAnimator::State wing;  // 翼の状態
    float lift = 0.0f;               // 打ち下ろしで体が浮く割合（0〜1）
    float headThrow = 0.0f;          // 打ち下ろしで首があおられる割合（0〜1）
  };

  // 基準の位置から、レールの水平な右・上・前の向きで offset だけ動かした位置
  Vector3 ToWorld(const Vector3 &base, const Vector3 &offset) const;

  void UpdateDragon();
  void UpdateWing();
  void UpdateCamera();

  // 今の時刻に打っている一打ち
  const StruggleBeat &CurrentBeat() const;

  // 動きの時間 motionTime でのドラゴンの位置
  Vector3 DragonPositionAt(float motionTime) const;

  // 動きの時間（ゆっくりの間は遅く進む）で、落ち始めてから下がった距離
  float FallDistance(float motionTime) const;

  // 実際の時間 time に対する動きの時間
  float MotionTimeAt(float time) const;

  std::unique_ptr<GameCamera> camera_;

  bool isActive_ = false;
  bool isTransitionCued_ = false; // 合図を出したか
  bool isCueReady_ = false;       // 合図を受け取られるのを待っているか
  float timer_ = 0.0f;            // 被弾してからの実際の時間（秒）
  float motionTime_ = 0.0f;       // 被弾してからの動きの時間（秒。ゆっくりの間は遅く進む）

  // Start の時点の基準
  Vector3 startPos_{};
  Vector3 startRot_{};
  Vector3 railForward_ = {0.0f, 0.0f, 1.0f}; // 水平にした前
  Vector3 railRight_ = {1.0f, 0.0f, 0.0f};   // 水平にした右
  Vector3 railCameraPos_{};
  Vector3 railLookPos_{};
  float railCameraFov_ = 0.45f;
  float fallSpeed_ = 0.0f; // 落ちる速さの上限（単位/秒。雲海へ沈みきる時刻から逆算する）

  // 時刻を止めているか（ImGui のつまみで時刻を動かして構図を見る）
  bool isPaused_ = false;

  // 左右の翼が今打ち下ろしている強さ（0〜1）
  float leftPush_ = 0.0f;
  float rightPush_ = 0.0f;
  // 打ち下ろしで浮いた高さ（落ちる道筋からの上向きのずれ）
  float lift_ = 0.0f;
  // 一打ちごとの、左右の翼の付け根の角度の最小・最大（度。測った値を ImGui に出す）
  struct WingAngleRange {
    float leftMin = 0.0f;
    float leftMax = 0.0f;
    float rightMin = 0.0f;
    float rightMax = 0.0f;
    bool isRecorded = false;
  };
  std::array<WingAngleRange, 4> beatAngles_{};

  // 今のドラゴンの位置・向き・翼
  Vector3 dragonPos_{};
  Vector3 dragonRot_{};
  DragonWingAnimator::State wingState_;
  DragonWingAnimator::Gesture gesture_;

  //---- 時間 ----
  float slowSec_ = 0.4f;            // 時間がゆっくりになる長さ（秒）
  float slowScale_ = 0.3f;          // ゆっくりの間の時間の速さ
  float slowRecoverSec_ = 0.1f;     // ゆっくりから普通の速さへ戻る長さ（slowSec_ の終わりに含む、秒）
  float limpStartTime_ = 2.2f;      // 翼が畳まれ始める時刻（秒）
  float limpEndTime_ = 2.6f;        // 力が抜けきる時刻（秒）
  float pullStartTime_ = 2.5f;      // カメラが上へ引き始める時刻（秒）
  float transitionTime_ = 3.3f;     // 雲の遷移を始める時刻（秒）
  float skipLockSec_ = 0.5f;        // 被弾してから飛ばせるようになるまでの時間（秒）
  float hudFadeSec_ = 0.3f;         // HUD が消えるまでの時間（秒）

  //---- ドラゴンの動き ----
  float hitPushDistance_ = 1.0f;    // 被弾で右へ弾かれる距離
  float hitLift_ = 0.4f;            // 被弾で浮き上がる高さ
  float hitPushSec_ = 0.3f;         // 弾かれきるまでの動きの時間（秒）
  float driftSpeed_ = 6.0f;         // 被弾した瞬間の前へ進む速さ（単位/秒）
  float driftDecaySec_ = 0.8f;      // 前へ進む勢いが落ちる速さ（指数関数の時定数、秒）
  float fallAccelSec_ = 1.0f;       // 落ちる速さが上限に届くまでの動きの時間（秒）
  float sinkDepth_ = 6.0f;          // 雲の遷移を始める時刻に、雲海の面から沈んでいる深さ
  float minFallDistance_ = 20.0f;   // 落ちる距離の下限（雲海が近いときに使う）
  float hitRoll_ = 0.7f;            // 弾かれて傾く角度（ラジアン）
  float limpRoll_ = 0.35f;          // 力が抜けたあとの傾き（ラジアン）
  float hitPitch_ = -0.3f;          // 弾かれた瞬間の機首の角度（ラジアン。負で上を向く）
  float limpPitch_ = 0.6f;          // 力が抜けたときの機首の角度（ラジアン。正で下を向く）
  float sinkPitch_ = 0.9f;          // 雲へ沈むときの機首の角度（ラジアン）
  float struggleRoll_ = 0.35f;      // 左右の翼の打ち下ろしの差 1 のときに体が傾く角度（ラジアン）
  float liftSpeed_ = 9.0f;          // 打ち下ろしの強さ 1・浮く割合 1 のときに浮く速さ（単位/秒）
  float liftReturnSec_ = 0.3f;      // 浮いた高さが落ちる道筋へ戻る速さ（指数関数の時定数、秒）
  float heavePitch_ = 0.25f;        // 打ち下ろしで機首が上がる角度（ラジアン。浮く割合を掛ける）

  //---- 翼（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合, 右の遅れ, 折れ, 振り回され）----
  DragonWingAnimator::State hitWing_ = {0.2f, 1.0f, 0.4f, 0.0f, 1.0f, 0.3f};  // 被弾
  DragonWingAnimator::State limpWing_ = {0.0f, 0.5f, 0.9f, 0.0f, 1.0f, 0.0f}; // 畳まれる
  // もがく一打ち：がむしゃらに大振り 2 回 → 大振りだが遅く翼が折れる → 片翼しか上がらない
  std::array<StruggleBeat, 4> beats_ = {{
      {0.4f, {1.3f, 2.6f, 0.0f, 0.3f, 1.0f, 1.0f, 0.08f, 0.0f, 1.0f}, 1.0f, 1.0f},
      {0.75f, {1.25f, 2.8f, 0.0f, -0.4f, 1.0f, 1.0f, 0.12f, 0.2f, 1.0f}, 0.7f, 0.8f},
      {1.1f, {1.0f, 1.6f, 0.1f, 0.25f, 1.0f, 1.0f, 0.1f, 0.7f, 0.6f}, 0.25f, 0.5f},
      {1.7f, {0.6f, 1.1f, 0.2f, 0.9f, 1.0f, 0.6f, 0.22f, 1.0f, 0.3f}, 0.0f, 0.2f},
  }};
  float limpCollapse_ = 0.6f;    // 力が抜けたときの、首が垂れる重み
  float struggleResponse_ = 20.0f; // もがく間、翼の状態が一打ちの値へ近づく速さ（1/秒）

  //---- カメラ（オフセットはドラゴンからの右・上・前）----
  float sideMoveStartTime_ = 0.1f; // 横へ回り込み始める時刻（秒）
  float sideMoveSec_ = 0.8f;       // 横へ回り込む時間（秒）
  Vector3 sideCameraOffset_ = {7.0f, 1.0f, 1.5f};
  Vector3 sideLookOffset_ = {0.0f, 0.3f, 0.5f};
  Vector3 pullCameraOffset_ = {3.0f, 14.0f, -3.0f}; // 引き始めたときのドラゴンの位置から
  float pullSec_ = 0.8f;       // 上へ引ききるまでの時間（秒）
  float hitShake_ = 0.3f;      // 被弾の瞬間のカメラの揺れの大きさ
  float hitShakeSec_ = 0.3f;   // 揺れが収まるまでの時間（秒）
};
