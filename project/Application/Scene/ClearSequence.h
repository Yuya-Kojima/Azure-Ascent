#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Effect/FractureDebris.h" // 破片の調整値 FractureDebris::Params・Look（入れ子の型）を返すため
#include "Math/MathUtil.h"
#include "Scene/StageLighting.h"
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CaptionCard;
class ClearFlight;
class LineRenderer;
class GameCamera;
class ICamera;
class Object3d;
class Object3dRenderer;
class RailCamera;
class Sprite;
class SpriteRenderer;

/// <summary>
/// クリア演出（ボスへの最後の一撃からリザルトの直前まで）
/// 最後の一撃で一瞬止めてスロー → カメラがボスの斜め前へ寄り始めると同時に、体のあちこちが順に爆発する
///   （爆発のたびに爆発した所が下がるように向きがガクッと崩れ、体が赤く光る。爆発の間隔はだんだん縮む）
/// → 大爆発で体が破片になって飛び散り、光の粒も外へ飛ぶ → 粒が真ん中へ吸い寄せられて光の玉が育ち、一瞬縮む
/// → 撃つ瞬間に遠く低い位置へカットし、光の柱がボスから真上へ一瞬で伸びて止まる
/// → 柱が細くなって消え、その分だけ天辺の光の玉がふくらむ（収束）
/// → 天辺の玉がとても小さくなる（間）
/// → 天辺の玉が一気に弾けて太陽が強く光り、そこから晴れが円で広がる（爆発）
/// → ドラゴンがカメラの脇をかすめて太陽へ駆け上がり、太陽の前で翼を広げて浮く瞬間に「STAGE CLEAR」のカードが光で現れる
/// → ドラゴンは取り返した空を気ままに飛び回り（ClearFlight）、カメラは位置を変えずに向きだけ遅れて追う。余韻の終わりにリザルトの合図を出す
/// 位置はすべて、Start の時点のボスの位置・大きさと、レールカメラからボスへの水平な向きを基準に求める
/// </summary>
class ClearSequence {
public:
  /// <summary>
  /// 演出の出来事（受け取ったシーンが音・ボス・リザルトを動かす）
  /// </summary>
  enum class Cue {
    BossBlast,  // 大爆発した（ボスの体を破片にして消す）
    Burst,      // 天辺の光が爆発した（晴れが広がり始める）
    Result,     // 余韻が終わった（リザルトを出す）
  };

  ClearSequence();
  ~ClearSequence();

  /// <summary>
  /// 初期化（上下の黒い帯・文字のカードと、光の玉・柱のモデルを作り、調整値を読み込む）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer,
                  Object3dRenderer *object3dRenderer);

  /// <summary>
  /// 演出を始める
  /// </summary>
  /// <param name="railCamera">最後の一撃の瞬間のレールカメラ（最初のカメラの位置と向きに使う）</param>
  /// <param name="bossPos">ボスの位置</param>
  /// <param name="bossSize">ボスの大きさ（カメラと光の大きさの単位にする）</param>
  /// <param name="bossRotation">ボスの向き（オイラー角。爆発で崩す前の向き）</param>
  /// <param name="blastPoints">爆発する体の表面の点の表（モデルの座標。大きさ 1。並びは爆発する順）</param>
  void Start(const RailCamera &railCamera, const Vector3 &bossPos,
             float bossSize, const Vector3 &bossRotation,
             const std::vector<Vector3> &blastPoints);

  /// <summary>
  /// 止める（演出の途中で Play をやめたとき）
  /// </summary>
  void Stop();

  /// <summary>
  /// 更新（時刻を進め、カメラ・光・ドラゴンを決め、出来事を積む）
  /// </summary>
  /// <param name="isSkipPressed">飛ばす入力があったか（あったら最後の構図へ進む）</param>
  void Update(bool isSkipPressed);

  /// <summary>
  /// 飛ばせる間か（最後の一撃から飛ばせるようになるまでの時間が過ぎ、リザルトの合図を出す前）
  /// </summary>
  bool CanSkip() const {
    return isActive_ && !isPaused_ && timer_ >= skipLockSec_ &&
           timer_ < ResultTime();
  }

  /// <summary>
  /// 光の玉と柱の描画（3D。不透明な物と一緒に描く）
  /// </summary>
  void Draw3D();

  /// <summary>
  /// 上下の黒い帯と文字のカードの描画（HUD より手前）
  /// </summary>
  void Draw2D();

  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 前に受け取ってから積んだ出来事を、起きた順に返して空にする
  /// </summary>
  std::vector<Cue> TakeCues();

  /// <summary>
  /// 世界（敵・弾・翼）の時間の速さ（1 で普通の速さ、0 で止まる）
  /// </summary>
  float GetTimeScale() const;

  /// <summary>
  /// HUD の不透明度（最後の一撃から消えていく）
  /// </summary>
  float GetHudAlpha() const;

  /// <summary>
  /// 演出のカメラ
  /// </summary>
  const ICamera *GetCamera() const;

  /// <summary>
  /// 晴れの進み具合（空・光・霧へ渡す）
  /// </summary>
  StageLighting::ClearState GetClearState() const;

  //---- ボス ----
  /// <summary>
  /// 爆発で崩れたボスの向き（ワールドの回転）
  /// </summary>
  Quaternion GetBossOrientation() const { return BossOrientationAt(timer_); }
  /// <summary>
  /// ボスを赤く光らせる色（HDR）と割合（0〜1）
  /// </summary>
  Vector4 GetBossFlashColor() const { return ToFlashColor(); }
  float GetBossFlash() const { return FlashAt(timer_); }
  /// <summary>
  /// 大爆発を見せる時刻か（飛ばして柱より後へ進んだときは、破片を出さずにボスを消すだけにする）
  /// </summary>
  bool IsBlastVisible() const { return isActive_ && timer_ < PillarStart(); }
  /// <summary>
  /// 大爆発の破片の調整値（寿命は柱へカットするまで）と見た目
  /// </summary>
  FractureDebris::Params GetDebrisParams() const;
  FractureDebris::Look GetDebrisLook(const Vector4 &bossColor) const;

  //---- ドラゴン ----
  /// <summary>
  /// ドラゴンを演出の経路で飛ばしているか（余韻の場面から）
  /// </summary>
  bool IsDragonFlying() const { return isActive_ && timer_ >= AfterglowStart(); }
  const Vector3 &GetDragonPosition() const { return dragonPos_; }
  const Vector3 &GetDragonRotation() const { return dragonRot_; }
  const Quaternion &GetDragonOrientation() const { return dragonOrientation_; }
  const DragonWingAnimator::State &GetWingState() const { return wing_; }
  // ドラゴンの逆光の縁の強さ（マテリアルの backRimScale。晴れの広がりに合わせて強める）
  float GetDragonBackRimScale() const;

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// 確認用の線（ドラゴンが飛んでいる間の、通った跡・行き先・リザルトの画面の枠）
  /// </summary>
  void DrawDebugLines(LineRenderer &lines) const;

  /// <summary>
  /// 確認用に、ドラゴンが飛ぶ所を真上から見下ろすカメラの位置と角度（ドラゴンが飛んでいないときは false）
  /// </summary>
  bool GetTopView(Vector3 &translate, Vector3 &rotate) const;

  /// <summary>
  /// 調整値を JSON から読み込む・JSON へ保存する
  /// </summary>
  void LoadConfig();
  void SaveConfig();

private:
  // カメラの位置・見る点・画角
  struct Shot {
    Vector3 position{};
    Vector3 look{};
    float fov = 0.45f;
  };

  // 調整値の float を、JSON のキーと一緒に1つずつ visit に渡す（読み込み・保存で共通）
  void VisitConfigValues(
      const std::function<void(const std::string &key, float &value)> &visit);

  //---- 場面の始まる時刻（最後の一撃からの秒。前の場面の秒を足していく） ----
  // 壊れていく場面は、カメラが寄り始めると同時に始まる（寄る間も爆発が続く）
  float ApproachStart() const { return hitSec_; }
  float BreakStart() const { return ApproachStart(); }
  float BlastStart() const { return BreakStart() + breakSec_; }
  float GatherStart() const { return BlastStart() + blastSec_; }
  float PillarStart() const { return GatherStart() + gatherSec_; }
  float ConvergeStart() const { return PillarStart() + pillarSec_; }
  float HoldStart() const { return ConvergeStart() + convergeSec_; }
  float BurstStart() const { return HoldStart() + holdSec_; }
  float AfterglowStart() const { return BurstStart() + clearSec_; }
  float ResultTime() const { return AfterglowStart() + afterglowSec_; }

  // 基準の位置から、水平な右・上・前（レールカメラからボスへ）の向きで offset だけ動かした位置
  Vector3 ToWorld(const Vector3 &base, const Vector3 &offset) const;
  // offset の向きを、水平な右・上・前の向きからワールドの向きにする
  Vector3 ToWorldDirection(const Vector3 &offset) const;

  // 時刻 time の柱の先の高さ（ボスの位置から上へ）
  float PillarLengthAt(float time) const;
  // 寄った構図・遠くから柱を見る構図（柱からあとはずっとこのカメラ）
  Shot NearShotAt(float time) const;
  Shot FarShot() const;
  // 晴れた空の太陽の向き（遠くのカメラから柱の天辺が見える向き。晴れの円の中心にもする）
  Vector3 SunDirection() const;
  Shot ShotAt(float time) const;

  // ドラゴンが飛び始める時刻
  float DragonEnterTime() const { return AfterglowStart() + dragonEnterSec_; }

  // 時刻 time の晴れの円の半径（度）。爆発から画面の端まで一気に広がり、そのあと空の反対側まで抜ける
  float SpreadRadiusDegAt(float time) const;
  // 時刻 time の晴れの広がり（0〜1。境目が画面の端 frameOutDeg_ に届くまでの割合。光・霧・明るさを晴れへ寄せる割合にもする）
  float SpreadAt(float time) const;
  // 時刻 time の、晴れた空の太陽の強さに掛ける値（爆発の瞬間に強く、だんだん元に戻る）
  float SunBoostAt(float time) const;
  // 時刻 time の、境目の闇の濃さに掛ける値（画面を抜けるまで 1、抜ける間に 0 へ）
  float EdgeDarkAt(float time) const;
  // 時刻 time の、柱が細くなった割合（0: 収束の始め 〜 1: 柱が消えきる）
  float ConvergeAt(float time) const;
  // 時刻 time のカメラの揺れ
  Vector3 ShakeAt(float time) const;

  //---- 壊れていく・大爆発・集まる ----
  // 小爆発の時刻の表（最後の一撃からの秒。間隔は explosionIntervalRatio_ の割合で縮む）
  std::vector<float> ExplosionTimes() const;
  // index 番目の小爆発で爆発する体の表面の点（モデルの座標。ボスの大きさ 1）
  const Vector3 &ExplosionPoint(size_t index) const;
  // index 番目の小爆発で向きが崩れる回転の軸（モデルの座標）と角度（ラジアン）
  Vector3 TiltAxis(size_t index) const;
  float TiltAngle(size_t index) const;
  // 時刻 time のボスの向き（Start の向きに、それまでの爆発の崩れを重ねる）
  Quaternion BossOrientationAt(float time) const;
  // ボスのモデルの座標の点を、時刻 time のボスの位置・向き・大きさでワールドの位置にする
  Vector3 BossPointToWorld(const Vector3 &point, float time) const;
  // 時刻 time のボスを赤く光らせる割合（爆発のたびに光って消え、終わりに近づくほど赤いまま残る）
  float FlashAt(float time) const;
  Vector4 ToFlashColor() const;
  // 時刻 from から to へ進んだときに通った小爆発・大爆発のパーティクルを出す
  void PlayExplosionsBetween(float from, float to);

  // 光の粒 index の、外へ飛ぶ向き・飛ぶ距離（ボスの大きさが単位）・戻り始めの遅れの割合（0〜1）
  Vector3 MoteDirection(size_t index) const;
  float MoteDistance(size_t index) const;
  float MoteDelayRatio(size_t index) const;
  // 光の粒 index の、時刻 time の中心からの距離（ボスの大きさが単位）と、戻りきった割合（0〜1）
  float MoteRadiusAt(size_t index, float time) const;
  float MoteReturnAt(size_t index, float time) const;
  // 時刻 time に粒が戻ってきた割合（すべての粒の平均。光の玉の育ち方に使う）
  float GatheredAt(float time) const;

  void UpdateCamera();
  void UpdateDragon();
  // ClearFlight を時刻 timer_ まで進める（時刻が戻ったら最初からやり直す）
  void AdvanceFlight();
  void UpdateCard();
  // 上下の黒い帯：カメラがボスへ寄り始めると閉じ、リザルトの時刻に開ききる
  void UpdateLetterbox();
  void UpdateLights();
  // 時刻 from から to へ進んだときに通った出来事を積む
  void PushCuesBetween(float from, float to);

  std::unique_ptr<GameCamera> camera_;
  std::unique_ptr<Object3d> orb_;      // ボスの中心に集まる光の玉
  static constexpr size_t kMoteCount_ = 48;              // 光の粒の数
  std::array<std::unique_ptr<Object3d>, kMoteCount_> motes_; // 大爆発で飛び散り、真ん中へ戻る光の粒
  bool isMoteVisible_ = false;
  std::unique_ptr<Object3d> pillar_;   // 光の柱
  std::unique_ptr<Object3d> tipOrb_;   // 柱の天辺で光る玉
  std::unique_ptr<Sprite> letterboxTop_;    // 上の黒い帯
  std::unique_ptr<Sprite> letterboxBottom_; // 下の黒い帯
  std::unique_ptr<CaptionCard> card_;  // 「STAGE CLEAR」「第一空域　奪還」のカード
  std::unique_ptr<ClearFlight> flight_; // 晴れた空を自由に飛ぶドラゴンと、それを追うカメラの向き
  bool isFlightReset_ = false;         // flight_ を今の演出の基準でやり直したか
  bool isOrbVisible_ = false;
  bool isPillarVisible_ = false;
  bool isTipOrbVisible_ = false;

  bool isActive_ = false;
  bool isPaused_ = false; // 時刻を止めているか（ImGui のつまみで時刻を動かして構図を見る）
  float timer_ = 0.0f;    // 最後の一撃からの実際の時間（秒）
  std::vector<Cue> cues_;

  // Start の時点の基準
  Vector3 bossPos_{};
  float bossSize_ = 1.0f;
  Quaternion bossBaseOrientation_{0.0f, 0.0f, 0.0f, 1.0f}; // 爆発で崩す前のボスの向き
  std::vector<Vector3> blastPoints_;     // 爆発する体の表面の点の表（Boss::GetBlastPoints）
  Vector3 forward_ = {0.0f, 0.0f, 1.0f}; // レールカメラからボスへの水平な向き
  Vector3 right_ = {1.0f, 0.0f, 0.0f};   // 水平な右
  Shot railShot_;                        // 最後の一撃の瞬間のレールカメラ

  // 今のドラゴンの位置・向き・翼
  Vector3 dragonPos_{};
  Vector3 dragonRot_{};
  Quaternion dragonOrientation_{0.0f, 0.0f, 0.0f, 1.0f};
  DragonWingAnimator::State wing_{};

  //---- 時間（秒） ----
  float hitSec_ = 0.8f;          // 最後の一撃（止める → スロー）
  float hitStopSec_ = 0.12f;     // 当たった瞬間に世界を止める時間
  float slowScale_ = 0.25f;      // スローの間の時間の速さ
  float slowRecoverSec_ = 0.2f;  // スローから普通の速さへ戻す長さ（hitSec_ の終わりに含む）
  float approachSec_ = 1.5f;     // 寄る
  float breakSec_ = 3.0f;        // 壊れていく（寄り始めから。小爆発が続く）
  float blastSec_ = 0.4f;        // 大爆発（破片と光の粒が外へ飛ぶ）
  float gatherSec_ = 1.5f;       // 集まる（光の粒が真ん中へ戻り、光の玉が育つ）
  float pillarSec_ = 0.35f;      // 柱（光の柱が天辺まで伸びる）
  float convergeSec_ = 1.8f;     // 収束（柱が細くなって消えきるまで）
  float holdSec_ = 0.8f;         // 間（柱が消えてから、天辺の玉が爆発するまで）
  float clearSec_ = 0.4f;        // 爆発してから、ドラゴンの場面（余韻）に移るまで
  float afterglowSec_ = 6.1f;    // 余韻（ドラゴンが太陽の前を過ぎ、文字のカードを読ませるまで）
  float skipLockSec_ = 0.5f;     // 最後の一撃から飛ばせるようになるまで
  float hudFadeSec_ = 0.3f;      // HUD が消えるまで

  //---- 上下の黒い帯 ----
  float letterboxAspect_ = 2.35f;   // 帯で隠したあとの画面の横と縦の比
  float letterboxInSec_ = 0.4f;     // カメラが寄り始めてから、帯が出きるまで（秒）
  float letterboxOutSec_ = 0.4f;    // 帯が開ききるまで（秒。リザルトの時刻に開ききる）

  //---- 晴れ ----
  float spreadStartDeg_ = 5.0f;  // 爆発した瞬間の晴れの円の半径（度）
  float sunBoost_ = 3.0f;        // 爆発した瞬間の、晴れた空の太陽の強さの倍率
  float sunBoostFadeSec_ = 0.4f; // 爆発してから、太陽の強さが元に戻るまで（秒）
  float burstSec_ = 1.0f;        // 爆発してから、境目が画面の端まで出るまで（出始めが速い）
  float frameOutDeg_ = 70.0f;    // 境目が画面の端の外へ出る半径（度。太陽から見た画面の遠い角まで）
  float finishSec_ = 0.8f;       // 抜ける（境目が画面の外を空の反対側まで進み、闇が消えるまで）

  //---- カメラ（オフセットはボスの大きさを 1 とした、右・上・前の値。前はレールカメラからボスへの向き）----
  Vector3 nearCameraOffset_ = {1.2f, 0.33f, -2.5f}; // 寄ったカメラの位置（ボスの斜め前。ボスを上下の黒い帯の内側に収める）
  Vector3 nearLookOffset_ = {0.0f, 0.1f, 0.0f};     // 寄ったカメラの見る点
  float nearFov_ = 0.6f;            // 寄ったカメラの画角（ラジアン）
  float approachLookRatio_ = 0.6f;  // 寄る時間のうち、見る点をボスへ向けきるまでの割合
  float pushIn_ = 0.85f;            // 壊れていく・大爆発・集まるの間に、寄ったカメラの距離をこの割合まで縮める
  float breakShake_ = 0.02f;        // 壊れていく終わりのカメラの揺れの大きさ（ボスの大きさが単位）
  float blastShake_ = 0.06f;        // 大爆発のカメラの揺れの大きさ（ボスの大きさが単位）
  float blastShakeSec_ = 0.6f;      // 大爆発の揺れが収まるまで（秒）
  Vector3 farCameraOffset_ = {0.6f, -0.5f, -50.0f}; // 柱を見る遠くのカメラの位置（ボスの手前の低い所。柱の根元から天辺までを上下の黒い帯の内側に収める）
  float farPitchDeg_ = 20.0f;       // 遠くのカメラの見上げる角度（度）
  float farYawDeg_ = 0.0f;          // 遠くのカメラの、ボスの向きから横へずらす角度（度。正で右）
  float farFov_ = 1.0f;             // 遠くのカメラの画角（ラジアン）
  float launchShake_ = 0.05f;       // 柱が伸び始めたときのカメラの揺れの大きさ（ボスの大きさが単位）
  float launchShakeSec_ = 0.4f;     // 柱が伸び始めたときの揺れが収まるまで（秒）

  //---- 小爆発（壊れていく）----
  float explosionFirstDelay_ = 0.1f;   // 寄り始めから最初の爆発まで（秒）
  float explosionInterval_ = 0.5f;     // 最初の爆発の間隔（秒）
  float explosionIntervalRatio_ = 0.8f; // 爆発のたびに、次の間隔をこの割合に縮める
  float explosionMinInterval_ = 0.08f; // 間隔の下限（秒）
  float explosionEndGap_ = 0.1f;       // 最後の小爆発から大爆発までに空ける時間（秒）
  float explosionScaleStart_ = 0.3f;   // 最初の小爆発のパーティクルの大きさの倍率
  float explosionScaleEnd_ = 0.6f;     // 最後の小爆発のパーティクルの大きさの倍率
  Vector3 explosionColor_ = {2.0f, 0.9f, 0.25f}; // 爆発の火の色（HDR）

  //---- 向きの崩れ ----
  float tiltKickDeg_ = 7.0f;       // 1 回の爆発で崩れる角度（度）
  float tiltJitter_ = 0.4f;        // 崩れる角度のばらつき（割合）
  float tiltRiseSec_ = 0.06f;      // 崩れきるまで（秒。ガクッと速く）
  float tiltRecoverSec_ = 0.25f;   // 少し戻るまでの時定数（秒）
  float tiltKeep_ = 0.35f;         // 戻ったあとに残る崩れの割合（爆発が重なるほど崩れていく）
  float tiltMaxDeg_ = 35.0f;       // 崩れの合計の上限（度）

  //---- 赤い点滅 ----
  Vector3 flashColor_ = {3.0f, 0.25f, 0.15f}; // 赤く光らせる色（HDR）
  float flashPeak_ = 0.9f;         // 爆発の瞬間に赤く光らせる割合
  float flashSec_ = 0.15f;         // 爆発の赤い光が消えていく時定数（秒）
  float flashFloor_ = 0.35f;       // 大爆発の直前に、赤いまま残る割合（壊れていくにつれて増える）

  //---- 大爆発 ----
  float blastScale_ = 2.0f;        // 真ん中の大きな爆発のパーティクルの大きさの倍率
  float blastPointScale_ = 0.8f;   // 体の表面の点での爆発のパーティクルの大きさの倍率
  float debrisSpeed_ = 4.0f;       // 破片が外へ飛ぶ速さ（ボスの大きさ/秒）
  float debrisGravity_ = 0.15f;    // 破片の重力（ボスの大きさ/秒²）
  float debrisDrag_ = 0.7f;        // 破片の空気抵抗（1 秒あたりに速度が減る割合）
  float debrisSpin_ = 4.0f;        // 破片の回る速さの最大（ラジアン/秒）
  Vector3 debrisRimColor_ = {2.0f, 0.6f, 0.2f}; // 破片のふちの光の色（焼けた縁）
  float debrisRimIntensity_ = 1.0f; // 破片のふちの光の強さ
  float debrisRimPower_ = 2.0f;    // 破片のふちの光の鋭さ

  //---- 光の粒（大爆発で外へ飛び、集まるで真ん中へ戻る）----
  float moteDistance_ = 0.9f;      // 粒が飛ぶいちばん遠い距離（ボスの大きさが単位）
  float moteDistanceJitter_ = 0.5f; // 飛ぶ距離のばらつき（いちばん遠い距離に対する割合だけ近くなる）
  float moteOutSec_ = 0.4f;        // 大爆発から、粒が飛びきるまで（秒）
  float moteReturnSec_ = 0.6f;     // 1 つの粒が真ん中へ戻るのにかかる時間（秒。終わりほど速い）
  float moteScale_ = 0.03f;        // 粒の半径（ボスの大きさが単位）
  float moteIntensity_ = 4.0f;     // 粒の明るさ（光の色に掛ける）

  //---- 光の玉と柱 ----
  Vector3 glowColor_ = {1.0f, 0.8f, 0.45f}; // 光の色（温かい白〜金）
  float orbStartScale_ = 0.05f;     // 大爆発の瞬間の玉の半径（ボスの大きさが単位）
  float orbMaxScale_ = 0.35f;       // 粒が戻りきったときの玉の半径（ボスの大きさが単位）
  float orbIntensity_ = 3.0f;       // 玉の明るさ（光の色に掛ける）
  Vector3 orbRimColor_ = {1.0f, 0.4f, 0.08f}; // 玉の縁取りの色（HDR）
  float orbRimIntensity_ = 3.0f;    // 玉の縁取りの強さ（0 でなし）
  float orbRimPower_ = 4.0f;        // 玉の縁取りの鋭さ（大きいほど細い）
  float squeezeSec_ = 0.15f;        // 柱を撃つ直前に、玉がぎゅっと縮む時間（秒。集まるの終わりに含む）
  float squeezeScale_ = 0.5f;       // 縮みきったときの玉の大きさの割合
  float orbShrinkSec_ = 0.3f;       // 柱が伸び始めてから玉が柱の太さまで縮むまで（秒）
  float pillarRadius_ = 0.25f;      // 柱の半径（ボスの大きさが単位）
  float pillarHeight_ = 400.0f;     // 柱の天辺の高さ（ボスの位置から上へ）
  float pillarIntensity_ = 3.0f;    // 柱の明るさ（光の色に掛ける）
  float pillarPulse_ = 0.1f;        // 柱の太さの脈打つ割合
  float tipOrbScale_ = 25.0f;       // 天辺の玉の半径（収束の終わり）
  float tipOrbStartRatio_ = 0.3f;   // 柱が天辺に届いた瞬間の天辺の玉の大きさ（tipOrbScale_ に対する割合）
  float tipShrinkScale_ = 0.15f;    // 間の終わりの天辺の玉の大きさ（tipOrbScale_ に対する割合）
  float burstOrbScale_ = 150.0f;    // 爆発で天辺の玉が弾けて広がりきったときの半径
  float burstOrbSec_ = 0.3f;        // 爆発で天辺の玉が弾けて広がりながら消えるまで（秒）
  float tipOrbIntensity_ = 5.0f;    // 天辺の玉の明るさ（光の色に掛ける）

  //---- ドラゴン ----
  float dragonEnterSec_ = 0.3f;     // 余韻の始まりから、ドラゴンが飛び始めるまで（秒）
  float dragonBackRimScale_ = 1.0f; // 晴れきったときの、ドラゴンの逆光の縁の強さ

  //---- 文字のカード（ドラゴンが太陽の前を過ぎた時刻に出始め、リザルトの時刻に消えきる）----
  float cardGrowSec_ = 0.35f;        // 横線が伸びきり、上の文字が現れきるまで
  float cardLowerDelay_ = 0.12f;     // 上の文字から、下の文字が現れ始めるまでの遅れ
  float cardOutSec_ = 0.3f;         // 消えきるまで
  float cardLineY_ = 500.0f;        // 横線の高さ（画面上端から px）
  float cardLineWidth_ = 780.0f;    // 横線の長さ（px）
};
