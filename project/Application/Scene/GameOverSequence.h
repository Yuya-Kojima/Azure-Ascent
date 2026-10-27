#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <array>
#include <functional>
#include <memory>
#include <string>

class CaptionCard;
class GameCamera;
class ICamera;
class RailCamera;
class Sprite;
class SpriteRenderer;

/// <summary>
/// ゲームオーバーの空中の演出
/// 被弾して時間がゆっくりになる → カメラがレールを離れてドラゴンの横に並ぶ → 頭は空を見上げたまま、翼と脚でもがくが、
/// 打つたびに弱くなる → 脚は先に止まって尾と一緒に力が抜けて垂れ、最後の打ち下ろしで一度だけ掻く → 翼が畳まれる → カメラが上へ引き、ドラゴンが雲海へ沈む
/// → 見下ろしに「GAME OVER」のカードが出て、その後ろで場面が黒い幕で暗くなる → カードが閉じたら拠点への遷移の合図
/// 位置はすべて、Start の時点のドラゴンの位置と、レールの水平な右・前の向きを基準に求める
/// </summary>
class GameOverSequence {
public:
  GameOverSequence();
  ~GameOverSequence();

  /// <summary>
  /// 初期化（黒い幕と文字カードのスプライトを作る）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer);

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
  /// <param name="isSkipPressed">決定キーが押されたか（押されたら、すぐ拠点への遷移の合図を出す）</param>
  void Update(bool isSkipPressed);

  /// <summary>
  /// 黒い幕と文字カードの描画（HUD より手前）
  /// </summary>
  void Draw();

  /// <summary>
  /// 演出中か
  /// </summary>
  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 拠点への遷移を始める合図（一度だけ true を返す）
  /// </summary>
  bool ConsumeTransitionCue();

  /// <summary>
  /// 左右の翼が今打ち下ろしている強さ（0〜1）を渡す（Update の前に毎フレーム）
  /// 打ち下ろしに合わせて体を浮かせ、左右の差で体を揺さぶる
  /// </summary>
  void SetWingPush(float left, float right) {
    leftPush_ = left;
    rightPush_ = right;
  }

  /// <summary>
  /// 左右の翼の付け根の角度（度）を渡す（一打ちごとの上げきり・下ろしきりを測って ImGui に出す）
  /// </summary>
  void RecordWingAngle(float left, float right);

  /// <summary>
  /// 今の顔の向き（度。DragonWingAnimator::GetFaceAngle の値）を渡す（測った値を ImGui に出す）
  /// </summary>
  void SetFaceAngle(float angle) { faceAngle_ = angle; }

  /// <summary>
  /// 被弾した瞬間の、左右の翼の付け根の角度（度）を渡す（Start のすぐあとに呼ぶ。弾かれて跳ね上がる動きの始まりにする）
  /// </summary>
  void SetStartWingRootAngles(float left, float right) {
    startLeftRoot_ = left;
    startRightRoot_ = right;
  }

  /// <summary>
  /// 翼の付け根の角度（度。上が正）を一打ちの表から求める（翼の直接モードに渡す）
  /// </summary>
  /// <param name="isRight">右の翼なら true</param>
  /// <param name="delaySec">今の演出の時刻から何秒前の角度か</param>
  float WingRootAngle(bool isRight, float delaySec) const;

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

  /// <summary>
  /// 調整値を JSON から読み込む・JSON へ保存する（文字カードの値は含まない）
  /// </summary>
  void LoadConfig();
  void SaveConfig();

private:
  // 調整値の float を、JSON のキーと一緒に1つずつ visit に渡す（読み込み・保存で共通）
  void VisitConfigValues(
      const std::function<void(const std::string &key, float &value)> &visit);

  // もがく一打ち。前の一打ちの上げきりの角度から下ろしきりまで打ち下ろし、上げきりまで打ち上げて、次の一打ちまで止まる
  // 角度は翼の付け根（度。上が正）、時刻は被弾からの実際の秒
  struct StruggleBeat {
    float time = 0.0f;         // この一打ちを始める時刻（秒）
    float downSec = 0.0f;      // 打ち下ろす時間（秒）
    float downAngle = 0.0f;    // 下ろしきる角度（度）
    float upSec = 0.0f;        // 打ち上げる時間（秒）
    float leftUpAngle = 0.0f;  // 左の翼の上げきる角度（度）
    float rightUpAngle = 0.0f; // 右の翼の上げきる角度（度）
    float rightDelay = 0.0f;   // 右の翼が左より遅れて打つ時間（秒）
    float buckle = 0.0f;       // 翼の張りが保てない度合い（0〜1）
    float flail = 0.0f;        // 体が羽ばたきに振り回される度合い（0〜1）
    float lift = 0.0f;         // 打ち下ろしで体が浮く割合（0〜1）
    float tremble = 0.0f;      // 打ち上げの間に指と首を震わせる強さ（0〜1）
  };
  // 一打ちの表に置ける数
  static constexpr size_t kMaxBeatCount_ = 4;

  // 基準の位置から、レールの水平な右・上・前の向きで offset だけ動かした位置
  Vector3 ToWorld(const Vector3 &base, const Vector3 &offset) const;

  void UpdateDragon();
  void UpdateWing();
  void UpdateCamera();
  // 黒い幕と文字カード
  void UpdateScreen();

  // 使う一打ちの数（beatCount_ を 1〜kMaxBeatCount_ に収めたもの）
  size_t UsedBeatCount() const;
  // 時刻 time に打っている一打ち（もがき始める前は最初の一打ち）
  const StruggleBeat &BeatAt(float time) const;
  // 今の時刻に打っている一打ち
  const StruggleBeat &CurrentBeat() const { return BeatAt(timer_); }

  // 時刻 time の付け根の角度（度）。右の翼の遅れは含まない
  float RootAngleAt(bool isRight, float time) const;
  // 時刻 time の、被弾からもがき終わるまでの付け根の角度（度）
  float StruggleRootAngleAt(bool isRight, float time) const;

  // 時刻 time の、脚で掻く強さ（0〜1）
  float PaddleStrengthAt(float time) const;
  // 時刻 time までに掻いた周期の数（掻き始めてから。一掻きの長さが遅くなる分を含む）
  float PaddlePhaseAt(float time) const;
  // 時刻 time の、最後の一掻きで脚 leg（左前・右前・左後・右後）を振る割合（正で後ろ）
  float LastPaddleSwingAt(int leg, float time) const;

  // 動きの時間 motionTime でのドラゴンの位置
  Vector3 DragonPositionAt(float motionTime) const;

  // 動きの時間（ゆっくりの間は遅く進む）で、落ち始めてから下がった距離
  float FallDistance(float motionTime) const;

  // 実際の時間 time に対する動きの時間
  float MotionTimeAt(float time) const;

  std::unique_ptr<GameCamera> camera_;
  std::unique_ptr<Sprite> curtain_;   // 場面を暗くする黒い幕（画面全体）
  std::unique_ptr<CaptionCard> card_; // 「GAME OVER」と「第一空域　奪還ならず」
  float curtainAlpha_ = 0.0f;         // 黒い幕の不透明度

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
  // 被弾した瞬間の、左右の翼の付け根の角度（度）
  float startLeftRoot_ = 0.0f;
  float startRightRoot_ = 0.0f;
  // 今の顔の向き（度。DragonWingAnimator::GetFaceAngle の値。ImGui に出す）
  float faceAngle_ = 0.0f;
  // 一打ちごとの、左右の翼の付け根の角度の最小・最大（度。測った値を ImGui に出す）
  struct WingAngleRange {
    float leftMin = 0.0f;
    float leftMax = 0.0f;
    float rightMin = 0.0f;
    float rightMax = 0.0f;
    bool isRecorded = false;
  };
  std::array<WingAngleRange, kMaxBeatCount_> beatAngles_{};

  // 今のドラゴンの位置・向き・翼
  Vector3 dragonPos_{};
  Vector3 dragonRot_{};
  DragonWingAnimator::State wingState_;
  DragonWingAnimator::Gesture gesture_;

  //---- 時間 ----
  float slowSec_ = 0.4f;            // 時間がゆっくりになる長さ（秒）
  float slowScale_ = 0.3f;          // ゆっくりの間の時間の速さ
  float slowRecoverSec_ = 0.1f;     // ゆっくりから普通の速さへ戻る長さ（slowSec_ の終わりに含む、秒）
  float limpStartTime_ = 2.3f;      // 翼が畳まれ始める時刻（秒）
  float limpEndTime_ = 2.6f;        // 力が抜けきる時刻（秒）
  float pullStartTime_ = 2.5f;      // カメラが上へ引き始める時刻（秒）
  float sinkTime_ = 3.3f;           // ドラゴンが雲海の面から sinkDepth_ だけ沈みきる時刻（秒）
  float darkenStartTime_ = 3.1f;    // 黒い幕が下り始める時刻（秒）
  float darkenSec_ = 0.8f;          // 黒い幕で真っ黒になるまでの時間（秒）
  float skipLockSec_ = 0.5f;        // 被弾してから飛ばせるようになるまでの時間（秒）
  float hudFadeSec_ = 0.3f;         // HUD が消えるまでの時間（秒）

  //---- ドラゴンの動き ----
  float hitPushDistance_ = 1.0f;    // 被弾で右へ弾かれる距離
  float hitLift_ = 0.4f;            // 被弾で浮き上がる高さ
  float hitPushSec_ = 0.3f;         // 弾かれきるまでの動きの時間（秒）
  float driftSpeed_ = 6.0f;         // 被弾した瞬間の前へ進む速さ（単位/秒）
  float driftDecaySec_ = 0.8f;      // 前へ進む勢いが落ちる速さ（指数関数の時定数、秒）
  float fallAccelSec_ = 1.0f;       // 落ちる速さが上限に届くまでの動きの時間（秒）
  float sinkDepth_ = 6.0f;          // sinkTime_ に、雲海の面から沈んでいる深さ
  float minFallDistance_ = 20.0f;   // 落ちる距離の下限（雲海が近いときに使う）
  float hitRoll_ = 0.7f;            // 弾かれて傾く角度（ラジアン）
  float limpRoll_ = 0.35f;          // 力が抜けたあとの傾き（ラジアン）
  float hitPitch_ = -0.3f;          // 弾かれた瞬間の機首の角度（ラジアン。負で上を向く）
  float limpPitch_ = 0.6f;          // 力が抜けたときの機首の角度（ラジアン。正で下を向く）
  float sinkPitch_ = 0.9f;          // 雲へ沈むときの機首の角度（ラジアン）
  float struggleRoll_ = 0.0f;       // 左右の翼の打ち下ろしの差 1 のときに体が傾く角度（ラジアン）
  float liftSpeed_ = 9.0f;          // 打ち下ろしの強さ 1・浮く割合 1 のときに浮く速さ（単位/秒）
  float liftReturnSec_ = 0.3f;      // 浮いた高さが落ちる道筋へ戻る速さ（指数関数の時定数、秒）
  float heavePitch_ = 0.25f;        // 打ち下ろしで機首が上がる角度（ラジアン。浮く割合を掛ける）

  //---- 翼 ----
  // 翼の状態（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）。付け根の角度は直接モードで決めるので、強さと回数は使わない
  DragonWingAnimator::State hitWing_ = {1.0f, 1.0f, 0.1f, 0.0f, 1.0f, 0.3f};      // 被弾
  DragonWingAnimator::State struggleWing_ = {1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f}; // もがく（折れ・振り回されは一打ちの値）
  DragonWingAnimator::State limpWing_ = {1.0f, 1.0f, 0.9f, 0.0f, 1.0f, 0.0f};     // 畳まれる
  float hitRiseSec_ = 0.15f;  // 被弾で、付け根が上げきりまで跳ね上がる時間（秒。最初の一打ちの始まりに合わせる）
  float hitUpAngle_ = 35.0f;  // 被弾で跳ね上がる付け根の角度（度）
  // もがく一打ち：打ち下ろしは短く、打ち上げは長く重く震える。打つたびに遅く小さくなり、最後は右の翼が上がりきらない
  // 使うのは先頭から beatCount_ 個。残りは予備の値
  int beatCount_ = 3;
  std::array<StruggleBeat, kMaxBeatCount_> beats_ = {{
      {0.15f, 0.2f, -45.0f, 0.5f, 40.0f, 40.0f, 0.03f, 0.0f, 0.0f, 0.3f, 0.6f},
      {0.85f, 0.25f, -40.0f, 0.6f, 30.0f, 30.0f, 0.05f, 0.4f, 0.0f, 0.15f, 1.0f},
      {1.7f, 0.3f, -25.0f, 0.3f, 15.0f, -10.0f, 0.08f, 1.0f, 0.0f, 0.0f, 0.5f},
      {2.3f, 0.3f, -15.0f, 0.3f, 10.0f, -10.0f, 0.08f, 1.0f, 0.0f, 0.0f, 0.3f},
  }};
  float foldRootAngle_ = 5.0f;     // 畳まれたときの付け根の角度（度）
  float faceUpAngle_ = 0.0f;       // もがく間から沈みきるまで保つ顔の向き（度。翼の Gesture::faceUpAngle。0 で鼻先が斜め上を向く）
  float hangStartTime_ = 0.9f;     // 脚と尾が垂れ始める時刻（秒）
  float hangSec_ = 0.5f;           // 脚と尾が垂れきるまでの時間（秒）
  float neckTrembleRatio_ = 0.2f;  // 震えのうち首に効く割合（翼の Gesture::neckTrembleRatio）
  float struggleResponse_ = 20.0f; // 翼の状態（畳み具合・折れなど）が演出の値へ近づく速さ（1/秒）

  //---- 脚 ----
  // 4 本がばらばらに空を掻き、だんだん弱く遅くなって止まる → 垂れる → 3 打目の打ち下ろしで、垂れたまま最後に弱く遅く一掻きする
  float paddleStartTime_ = 0.15f;    // 掻き始める時刻（秒）
  float paddleRiseSec_ = 0.15f;      // 掻く強さが 1 になるまでの時間（秒）
  float paddleTiredTime_ = 1.25f;    // 掻く強さが paddleTiredStrength_ まで下がりきる時刻（秒）
  float paddleTiredStrength_ = 0.8f; // 下がりきったときの掻く強さ
  float paddleStopTime_ = 1.0f;      // 掻く強さが 0 になる時刻（秒）
  // 脚 1 本の一掻きの長さ（秒）。強さが 1 になったときの長さから、下がりきったときの長さへ遅くする
  float paddleStartSec_ = 0.45f;
  float paddleEndSec_ = 0.65f;
  // 最後の一掻き：脚ごとに、垂れた形のまま前へ出して後ろへ引き、垂れた位置に戻す。掻き始めと同じ順で 1 本ずつずらす
  float lastPaddleTime_ = 1.75f;     // 最初の脚が掻き始める時刻（秒）
  float lastPaddleSec_ = 0.4f;       // 脚 1 本の一掻きの長さ（秒）
  float lastPaddleLegDelay_ = 0.06f; // 掻く順で、1 本ごとに遅れる時間（秒）
  float lastPaddleStrength_ = 0.6f;  // 大きさ（掻く振れ幅に対する割合）
  // 脚ごとの大きさの割合（左前・右前・左後・右後）
  std::array<float, 4> lastPaddleLegWeights_ = {1.0f, 0.4f, 0.7f, 1.0f};

  //---- カメラ（オフセットはドラゴンからの右・上・前）----
  float sideMoveStartTime_ = 0.1f; // 横へ回り込み始める時刻（秒）
  float sideMoveSec_ = 0.45f;      // 横へ回り込む時間（秒）
  Vector3 sideCameraOffset_ = {7.0f, 1.0f, 1.5f};
  Vector3 sideLookOffset_ = {0.0f, 0.3f, 0.5f};
  Vector3 pullCameraOffset_ = {3.0f, 14.0f, -3.0f}; // 引き始めたときのドラゴンの位置から
  float pullSec_ = 0.8f;       // 上へ引ききるまでの時間（秒）
  float hitShake_ = 0.3f;      // 被弾の瞬間のカメラの揺れの大きさ
  float hitShakeSec_ = 0.3f;   // 揺れが収まるまでの時間（秒）
};
