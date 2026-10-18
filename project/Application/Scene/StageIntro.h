#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <memory>

class GameCamera;
class ICamera;
class RailCamera;
class BillboardParticleEmitter;

/// <summary>
/// ステージ開始演出（絵コンテの S4〜S7）
/// S4 雲が晴れ、雲海が盛り上がる → S5 ドラゴンが雲海を突き破って上昇し、カメラは下から見上げる
/// → S6 カメラがドラゴンの後ろ上へ上がり、前方の空を見せる → S7 ドラゴンは操作開始の位置へ、カメラはレールカメラへ移る
/// </summary>
class StageIntro {
public:
  StageIntro();
  ~StageIntro();

  /// <summary>
  /// 初期化（雲の粒のパーティクルを作る）
  /// </summary>
  void Initialize();

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
  /// 更新（ドラゴンの位置・向き、演出カメラ、雲の粒の発生）
  /// </summary>
  void Update();

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
  /// 調整用UI
  /// </summary>
  void DrawImGui();

  bool IsFinished() const { return isFinished_; }
  const ICamera *GetCamera() const;
  const Vector3 &GetDragonPosition() const { return dragonPos_; }
  const Vector3 &GetDragonRotation() const { return dragonRot_; }
  const Vector3 &GetDragonEndPosition() const { return dragonEndPos_; }
  // 今の場面での羽ばたきの状態（上昇: 強く速く → 止まる: 大きくゆっくり → 降りる: 畳み気味 → 通常）
  DragonWingAnimator::State GetWingState() const;

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

private:
  // 演出カメラ
  std::unique_ptr<GameCamera> camera_;
  // 雲の粒
  std::unique_ptr<BillboardParticleEmitter> cloudParticles_;

  // ステージに入ってからの経過時間（秒）
  float timer_ = 0.0f;
  bool isFinished_ = true;
  // 前のフレームで雲海を突き破っていたか
  bool hasBurst_ = false;

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
  float turnStartTime_ = 3.2f;  // S7 を始める時刻
  float turnSec_ = 1.4f;        // S7 にかける時間

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

  //===========================
  // 調整値：動き・見た目
  //===========================
  float risePitch_ = -1.2f;      // 上昇中の機首の角度（ラジアン。負で上を向く）
  float hoverPitch_ = -0.1f;     // 止まっている間の機首の角度（ラジアン）
  float maxTurnPitch_ = 0.5f;    // S7 で進む向きに合わせる機首の角度の上限（ラジアン）
  float bobHeight_ = 0.15f;      // 止まっている間の上下の揺れ幅
  float bobPeriod_ = 2.0f;       // 上下の揺れの周期（秒）
  float bobRoll_ = 0.06f;        // 止まっている間の体の傾きの大きさ（ラジアン）
  float followLagSec_ = 0.06f;   // S5 でカメラがドラゴンを追う遅れ（時定数、秒）
  float burstShake_ = 0.35f;     // 突き破った瞬間のカメラの揺れの大きさ
  float burstShakeSec_ = 0.35f;  // 揺れが収まるまでの時間（秒）
  float lowCameraFov_ = 0.87f;   // S4〜S6 のカメラの画角（ラジアン）
  Vector3 puffColor_ = {0.95f, 0.95f, 1.0f}; // 雲の粒の色

  //===========================
  // 調整値：羽ばたき（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）
  //===========================
  DragonWingAnimator::State riseWing_ = {1.0f, 2.6f, 0.0f, 0.0f, 1.0f, 0.8f};  // 上昇（S5）
  DragonWingAnimator::State hoverWing_ = {0.9f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f}; // 止まる（S6）
  DragonWingAnimator::State diveWing_ = {0.3f, 1.5f, 0.6f, 0.0f, 1.0f, 0.0f};  // 降りる（S7 の前半）
  DragonWingAnimator::State endWing_ = {0.6f, 1.5f, 0.0f, 0.0f, 1.0f, 0.0f};   // 操作開始の前（S7 の後半）
  float diveWingRatio_ = 0.6f; // S7 のうち、畳み気味で降りる割合
};
