#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include "Scene/CloudSeaEvents.h"
#include <memory>

class GameCamera;
class ICamera;
class RailCamera;
class BillboardParticleEmitter;
class SpriteRenderer;
class CaptionCard;

/// <summary>
/// ステージ開始演出（絵コンテの S4〜S7）
/// S4 雲が晴れ、雲海が盛り上がる → S5 ドラゴンが雲海を突き破って上昇し、カメラは下から見上げる
/// → S6 カメラがドラゴンの後ろ上へ上がり、前方の空を見せる → S7 ドラゴンは操作開始の位置へ、カメラはレールカメラへ移る
/// → S8 レールが動き出す中でドラゴンが溜めて空へ吠え、一打ちで飛び出す。操作は飛び出しに合わせて少しずつ効かせる
/// 位置はすべて、Start の時点のレールの位置と向きを基準に求め、今のレールの位置と向きへ移す
/// </summary>
class StageIntro {
public:
  /// <summary>
  /// 吠える間のカメラ
  /// </summary>
  enum class RoarCamera {
    Follow,   // プレイ視点（レールカメラ）のまま
    Showcase, // 右後ろの低い位置から見上げ、飛び出しに合わせて後ろへ回り込む
  };

  StageIntro();
  ~StageIntro();

  /// <summary>
  /// 初期化（雲の粒のパーティクルと、目標カードを作る）
  /// </summary>
  /// <param name="cloudSeaEvents">雲海の出来事を登録する先</param>
  void Initialize(SpriteRenderer *spriteRenderer,
                  CloudSeaEvents *cloudSeaEvents);

  /// <summary>
  /// 演出を開始する
  /// </summary>
  /// <param name="railCamera">演出の最後に重ねるレールカメラ（停止中のもの）</param>
  /// <param name="dragonEndPos">演出終了時（操作開始時）のドラゴンの位置</param>
  /// <param name="dragonEndRot">演出終了時（操作開始時）のドラゴンの向き</param>
  /// <param name="seaHeight">雲海の面の高さ（ワールドのY）</param>
  void Start(const RailCamera &railCamera, const Vector3 &dragonEndPos,
             const Vector3 &dragonEndRot, float seaHeight);

  /// <summary>
  /// 飛ばす（溜めの直前まで進め、目標カードを消す。吠える合図は毎回見せる）
  /// </summary>
  void Skip();

  /// <summary>
  /// 止める（演出の途中で Play をやめたとき）
  /// </summary>
  void Stop();

  /// <summary>
  /// 更新（ドラゴンの位置・向き、演出カメラ、雲の粒の発生）
  /// </summary>
  /// <param name="railCamera">今のレールカメラ（進んだ距離と、最後に移るカメラの位置に使う）</param>
  void Update(const RailCamera &railCamera);

  /// <summary>
  /// 雲の粒の更新（ParticleManager の Update と Emit の間で呼ぶ）
  /// </summary>
  void UpdateParticles(const Matrix4x4 &viewMatrix,
                       const Matrix4x4 &projectionMatrix);

  /// <summary>
  /// 雲の粒の描画
  /// </summary>
  void DrawParticles();

  /// <summary>
  /// 目標カードの更新
  /// </summary>
  void UpdateCaption();

  /// <summary>
  /// 目標カードの描画
  /// </summary>
  void DrawCaption();

  /// <summary>
  /// 調整用UI
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// 雲海の出来事の調整用UI と、その場で起こすボタン
  /// </summary>
  /// <param name="focus">ドラゴンの位置</param>
  /// <param name="forward">盛り上がり・裂け目を置く前方</param>
  void DrawCloudSeaImGui(const Vector3 &focus, const Vector3 &forward);

  bool IsFinished() const { return isFinished_; }
  // 吠え声からの秒（鳴る前は負）
  float GetTimeSinceRoar() const { return timer_ - roarSoundTime_; }
  const ICamera *GetCamera() const;
  const Vector3 &GetDragonPosition() const { return dragonPos_; }
  const Vector3 &GetDragonRotation() const { return dragonRot_; }
  // 操作開始の位置（今のレールの位置と向きに合わせたもの）
  Vector3 GetDragonEndPosition() const;
  // 今の場面での羽ばたきの状態（上昇: 強く速く → 止まる: 大きくゆっくり → 降りる: 畳み気味 → 通常）
  DragonWingAnimator::State GetWingState() const;
  // 羽ばたきに重ねる姿勢（溜め → 吠える → 飛び出し）
  DragonWingAnimator::Gesture GetGesture() const;

  // レールの速さの割合（0: 止める 〜 1: 通常）
  float GetRailSpeedRatio() const;
  // 操作の効き具合（0: 演出が自機を動かす 〜 1: 操作だけで動かす）
  float GetControlRatio() const;
  // HUD の不透明度（0〜1）
  float GetHudAlpha() const;
  // 操作を効かせ始めたか
  bool IsControlStarted() const {
    return isFinished_ || timer_ >= controlStartTime_;
  }
  // 吠え声を鳴らす時刻を過ぎた最初の1回だけ true
  bool ConsumeRoarCue();

private:
  /// <summary>
  /// 終了位置を原点に、x: レールの右, y: 真上, z: レールの前 で表した座標をワールド座標に変換する
  /// </summary>
  Vector3 ToWorld(const Vector3 &origin, const Vector3 &local) const;

  /// <summary>
  /// S6 で止まっている間の上下の揺れ（y の量と、体の傾き）
  /// </summary>
  void CalcHover(float time, float &bobY, float &roll) const;

  /// <summary>
  /// ドラゴンの位置と向きを更新する
  /// </summary>
  void UpdateDragon();

  /// <summary>
  /// 演出カメラを更新する
  /// </summary>
  void UpdateCamera();

  /// <summary>
  /// 雲の粒を発生させる
  /// </summary>
  void EmitCloudParticles();

  /// <summary>
  /// 重ねる姿勢に合わせて、体を起こす・丸める（体の中心まわりに回し、上下させる）
  /// </summary>
  void ApplyGestureBody(const DragonWingAnimator::Gesture &gesture);

  /// <summary>
  /// 吠える声の大きさに合わせた震え・揺れの強さ（0〜1）
  /// </summary>
  float CalcTremble() const;

  /// <summary>
  /// 演出の終わる時刻（姿勢・操作・カメラのうち最後に終わるもの）
  /// </summary>
  float GetEndTime() const;

  /// <summary>
  /// Start の時点のレールを基準に求めた位置を、今のレールの位置と向きへ移す
  /// </summary>
  Vector3 FollowRail(const Vector3 &startPosition) const;

  /// <summary>
  /// 雲海の出来事を作る（startTime は出来事の時刻の秒）
  /// </summary>
  CloudSeaEvents::Event MakeSwellEvent(float startTime,
                                       const Vector3 &center) const;
  CloudSeaEvents::Event MakeTearEvent(float startTime,
                                      const Vector3 &center) const;

private:
  // 演出カメラ
  std::unique_ptr<GameCamera> camera_;
  // 雲の粒
  std::unique_ptr<BillboardParticleEmitter> cloudParticles_;
  // 目標カード
  std::unique_ptr<CaptionCard> text_;
  // 雲海の出来事の登録先
  CloudSeaEvents *cloudSeaEvents_ = nullptr;

  // ステージに入ってからの経過時間（秒）
  float timer_ = 0.0f;
  bool isFinished_ = true;
  // 前のフレームで雲海を突き破っていたか
  bool hasBurst_ = false;
  // 吠え声を鳴らす合図を出したか
  bool isRoarCued_ = false;

  // Start の時点のレールの位置と、今のレールの位置・向き（右・前は水平にしたもの）
  Vector3 railOrigin_ = {0.0f, 0.0f, 0.0f};
  Vector3 liveRailOrigin_ = {0.0f, 0.0f, 0.0f};
  Vector3 liveRailRight_ = {1.0f, 0.0f, 0.0f};
  Vector3 liveRailForward_ = {0.0f, 0.0f, 1.0f};
  float railYawDelta_ = 0.0f; // Start の時点からレールが左右に曲がった角度（ラジアン）
  // 今のレールカメラの位置・見る点・画角
  Vector3 liveRailCameraPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 liveRailLookPos_ = {0.0f, 0.0f, 0.0f};
  float liveRailCameraFov_ = 0.0f;

  // レールの向き（右・前は水平にしたもの）
  Vector3 railRight_ = {1.0f, 0.0f, 0.0f};
  Vector3 railForward_ = {0.0f, 0.0f, 1.0f};

  // Start で決まる位置
  Vector3 dragonEndPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 dragonEndRot_ = {0.0f, 0.0f, 0.0f};
  Vector3 hoverPos_ = {0.0f, 0.0f, 0.0f};        // S6 で止まる位置（上昇の頂点）
  Vector3 burstPos_ = {0.0f, 0.0f, 0.0f};        // 雲海を突き破る点
  Vector3 riseStartPos_ = {0.0f, 0.0f, 0.0f};    // 上昇を始める位置（雲海の下）
  Vector3 lowCameraPos_ = {0.0f, 0.0f, 0.0f};    // S4・S5 のカメラ位置
  Vector3 lowLookPos_ = {0.0f, 0.0f, 0.0f};      // S4 でカメラが見る点
  Vector3 revealCameraPos_ = {0.0f, 0.0f, 0.0f}; // S6 のカメラ位置
  Vector3 revealLookPos_ = {0.0f, 0.0f, 0.0f};   // S6 でカメラが見る点
  Vector3 railCameraPos_ = {0.0f, 0.0f, 0.0f};   // レールカメラの位置
  Vector3 railLookPos_ = {0.0f, 0.0f, 0.0f};     // レールカメラが見る点
  float railCameraFov_ = 0.0f;
  float riseStartTime_ = 0.0f;                   // 上昇を始める時刻（突き破る時刻から逆算）
  Vector3 awayFromCamera_ = {0.0f, 0.0f, 1.0f};  // S4・S5 のカメラから遠ざかる水平の向き

  // 現在の状態
  Vector3 dragonPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 dragonRot_ = {0.0f, 0.0f, 0.0f};
  Vector3 followLookPos_ = {0.0f, 0.0f, 0.0f};   // S5 でカメラが追う点（ドラゴンに遅れてついて行く）

  //===========================
  // 調整値：時刻（秒。ステージに入ってからの時刻）
  //===========================
  float burstTime_ = 0.9f;      // 雲海を突き破る時刻
  float swellLeadSec_ = 0.6f;   // 突き破る何秒前から雲海が盛り上がり始めるか
  float riseSec_ = 1.2f;        // 上昇の開始から頂点までの時間
  float craneStartTime_ = 1.8f; // S6 でカメラが上がり始める時刻
  float craneSec_ = 1.1f;       // S6 でカメラが上がりきるまでの時間
  float craneLookDelay_ = 0.5f; // S6 のカメラの移動のうち、見る点を前方の空へ移し始める割合
  float turnStartTime_ = 4.4f;  // S7 を始める時刻
  float turnSec_ = 1.4f;        // S7 にかける時間

  // S8 吠える・飛び出す（溜め → 吠える → 吠え続ける → 飛び出しの一打ち → 通常の飛行）
  float railStartTime_ = 5.0f;      // レールが動き出す時刻
  float railRampSec_ = 1.8f;        // レールが通常の速さになるまでの時間
  float crouchStartTime_ = 5.45f;   // 溜め始める時刻
  float crouchPeakTime_ = 5.75f;    // 溜めきる時刻
  float roarSoundTime_ = 5.8f;      // 吠え声を鳴らす時刻
  float roarPeakTime_ = 6.0f;       // 吠える姿勢になりきる時刻（声が大きくなる）
  float trembleFadeTime_ = 6.2f;    // 震えを弱め始める時刻（声が最大を過ぎる）
  float roarEndTime_ = 6.55f;       // 吠える姿勢を解き始める時刻
  float launchPeakTime_ = 6.75f;    // 飛び出しの一打ちで翼を打ち下ろしきる時刻
  float gestureEndTime_ = 7.1f;     // 通常の飛行姿勢に戻りきる時刻
  float hudStartTime_ = 6.3f;       // HUD を現し始める時刻（声が弱まり始める）
  float hudSec_ = 0.6f;             // HUD が現れきるまでの時間
  float controlStartTime_ = 6.85f;  // 操作と照準を効かせ始める時刻（一打ちのあと）
  float controlSec_ = 0.5f;         // 操作が効ききるまでの時間
  float cameraReturnTime_ = 6.55f;  // 吠えるカメラからレールカメラへ移り始める時刻
  float cameraReturnSec_ = 0.55f;   // レールカメラへ移りきるまでの時間

  //===========================
  // 調整値：位置
  // 「終了位置を原点に、x: レールの右, y: 真上, z: レールの前」で表す
  //===========================
  Vector3 hoverOffset_ = {-2.0f, 4.0f, -4.0f};       // S6 で止まる位置（終了位置から）
  float riseStartDepth_ = 6.0f;                      // 上昇を始める位置の、雲海の面からの深さ
  Vector3 lowCameraOffset_ = {3.0f, 3.0f, -10.0f};   // S4・S5 のカメラ位置（突き破る点から）
  float lowLookHeight_ = 2.0f;                       // S4 でカメラが見る点の、突き破る点からの高さ
  Vector3 revealCameraOffset_ = {1.5f, 2.0f, -8.0f}; // S6 のカメラ位置（止まる位置から）
  Vector3 revealLookOffset_ = {0.0f, 1.0f, 20.0f};   // S6 でカメラが見る点（止まる位置から）
  float diveDepth_ = 1.5f;                           // S7 で弧の中間が下がる量
  Vector3 roarCameraOffset_ = {3.5f, 0.6f, -7.0f};   // 吠えるカメラ（Showcase）の位置（終了位置から）
  Vector3 roarLookOffset_ = {0.0f, 2.0f, 1.0f};      // 吠えるカメラが見る点（終了位置から）
  Vector3 bodyPivot_ = {0.0f, 1.1f, 0.5f};           // 体を起こす中心（ドラゴンの原点から。x は使わない）

  //===========================
  // 調整値：動き・見た目
  //===========================
  float risePitch_ = -1.2f;      // 上昇中の機首の角度（ラジアン。負で上を向く）
  float hoverPitch_ = -0.1f;     // 止まっている間の機首の角度（ラジアン）
  float maxTurnPitch_ = 0.15f;   // S7 で進む向きに合わせる機首の角度の上限（ラジアン）
  float turnBank_ = 0.6f;        // S7 で横へ進むときの体の傾き（真横へ進むときの角度。ラジアン）
  float bobHeight_ = 0.15f;      // 止まっている間の上下の揺れ幅
  float bobPeriod_ = 2.0f;       // 上下の揺れの周期（秒）
  float bobRoll_ = 0.06f;        // 止まっている間の体の傾きの大きさ（ラジアン）
  float followLagSec_ = 0.06f;   // S5 でカメラがドラゴンを追う遅れ（時定数、秒）
  float burstShake_ = 0.35f;     // 突き破った瞬間のカメラの揺れの大きさ
  float burstShakeSec_ = 0.35f;  // 揺れが収まるまでの時間（秒）
  float lowCameraFov_ = 0.87f;   // S4〜S6 のカメラの画角（ラジアン）
  RoarCamera roarCamera_ = RoarCamera::Follow; // 吠える間のカメラ
  // 体の起こし（ラジアン。正で機首が下がる）と上下（上が正）。溜め・吠える・飛び出しの姿勢ごと
  float crouchBodyPitch_ = 0.1f;
  float roarBodyPitch_ = -0.35f;
  float launchBodyPitch_ = 0.14f;
  float crouchBodyLift_ = -0.1f;
  float roarBodyLift_ = 0.25f;
  float launchBodyLift_ = 0.15f;
  float roarShake_ = 0.07f;      // 吠える間のカメラの揺れの大きさ
  Vector3 puffColor_ = {0.30f, 0.30f, 0.33f}; // 雲の粒の色（HDR）

  //===========================
  // 調整値：雲海の出来事
  //===========================
  // 盛り上がり（突き破る点が中心。始まりは burstTime_ - swellLeadSec_、だんだん速く高くなる）
  float swellRadius_ = 12.0f;   // 広がりの半径
  float swellHeight_ = 3.0f;    // 高さ
  float swellCrack_ = 1.0f;     // ひびの濃さ
  float swellFallSec_ = 0.5f;   // 突き破ってから崩れて沈みきるまでの時間
  // 裂け目（突き破る点が中心。burstTime_ に開き始める）
  float tearRadius_ = 5.0f;     // 穴の最大の半径
  float tearLip_ = 1.2f;        // 縁のめくれの高さ
  float tearOpenSec_ = 0.2f;    // 開ききるまでの時間
  float tearHoldSec_ = 0.3f;    // 開いたままの時間
  float tearCloseSec_ = 1.6f;   // 閉じきるまでの時間
  // その場で起こすボタンで、盛り上がり・裂け目を置く前方の距離
  float triggerAheadDistance_ = 25.0f;

  //===========================
  // 調整値：羽ばたき（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）
  //===========================
  DragonWingAnimator::State riseWing_ = {1.0f, 2.6f, 0.0f, 0.0f, 1.0f, 0.8f};  // 上昇（S5）
  DragonWingAnimator::State hoverWing_ = {0.9f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f}; // 止まる（S6）
  DragonWingAnimator::State diveWing_ = {0.3f, 1.5f, 0.6f, 0.0f, 1.0f, 0.0f};  // 降りる（S7 の前半）
  DragonWingAnimator::State endWing_ = {0.6f, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f};   // 操作開始の前（S7 の後半）
  float diveWingRatio_ = 0.6f; // S7 のうち、畳み気味で降りる割合
};
