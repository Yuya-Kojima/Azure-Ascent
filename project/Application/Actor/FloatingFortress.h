#pragma once
#include "Actor/EnemyPart.h"
#include "Effect/BigEnemyDeath.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>

class FractureDebris;
class Object3d;
class Object3dRenderer;
class RailCamera;

/// <summary>
/// 浮かぶ砦。ワールドの道筋を、レールの進み具合 t に合わせてなぞる
/// 部品 8 個を持ち、全部壊れたら傾いて小さな爆発を起こしながら道筋を進んで耐え、
/// 画面の中で十分に離れ、飛び散る塊がカメラに近づきすぎない所まで来たら、最後に壊れた部品の所から割れ目が走って大爆発する
/// 道筋の終わりまで残ったら部品ごと消える
/// 本体はロックの対象ではなく、本体・甲板・砲座の台の箱がロックの陰になる
/// </summary>
class FloatingFortress : public IEnemyPartOwner {
public:
  static constexpr int kPartCount = 8;

  // 外へ知らせる出来事
  enum class Event {
    Killed,  // 部品を全部壊された（ヒットストップ）
    Exploded // 大爆発した（カメラを揺らす）
  };

  // 部品 1 個の決まり（役・砦の中の位置・半分の大きさ m）。砦の中の座標は x 右・y 上・z 前
  struct PartSpec {
    EnemyPart::Role role;
    Vector3 localPosition;
    Vector3 halfSize;
  };

  FloatingFortress();
  ~FloatingFortress() override;

  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="renderer">箱を描く描画係</param>
  /// <param name="railCamera">進み具合 t とレールの位置を読むカメラ</param>
  /// <param name="path">ワールドの道筋の点（点と点の間は同じ秒数）</param>
  /// <param name="spawnT">出たときのレールの進み具合</param>
  /// <param name="pathSeconds">道筋を走り切る秒数</param>
  /// <param name="heading">向き（y 回転のラジアン）。なければ進む向きを向く</param>
  void Initialize(Object3dRenderer *renderer, const RailCamera *railCamera,
                  const std::vector<Vector3> &path, float spawnT,
                  float pathSeconds, std::optional<float> heading);

  /// <summary>
  /// 部品の決まり（番号の順）
  /// </summary>
  static const std::array<PartSpec, kPartCount> &GetPartSpecs();

  /// <summary>
  /// 部品を渡す（番号は GetPartSpecs の順）
  /// </summary>
  void SetPart(int index, EnemyPart *part);

  /// <summary>
  /// 動かす（世界の時間が進むフレームだけ呼ぶ）
  /// </summary>
  void Update();

  /// <summary>
  /// 箱の行列だけを作り直す（世界が止まっているフレーム）
  /// </summary>
  void UpdateTransform();

  void Draw();

  /// <summary>
  /// 前の呼び出しから起きた出来事を取り出す
  /// </summary>
  std::vector<Event> TakeEvents();

  /// <summary>
  /// ロックの陰にする箱を足す（ワールド → 箱の中の座標で箱が -1〜1 になる行列）
  /// </summary>
  void AppendOccluders(std::vector<Matrix4x4> &worldToBoxes) const;

  /// <summary>
  /// 落ち切った・去り終わった
  /// </summary>
  bool IsFinished() const;

  // IEnemyPartOwner
  const Matrix4x4 &GetPartFrame(int) const override { return frame_; }
  Vector3 GetPartRotation(int) const override;
  bool CanPartFire() const override;
  void OnPartDestroyed(int index) override;

private:
  // 状態（FloatingFortress.cpp で定義する。砦の入れ子のクラスなので、砦の private な関数を呼べる）
  class State;         // 基底
  class FlyState;      // 道筋をなぞる
  class EnduringState; // 部品を全部壊されて、小さな爆発を起こしながら道筋を進み、見える位置まで耐える
  class DyingState;    // 割れ目が走ってから死亡演出が終わるまで（道筋は進み続ける）
  class FinishedState; // 終わり

  /// <summary>
  /// 状態を next に入れ替え、入ったときの準備をさせる
  /// </summary>
  void ChangeState(std::unique_ptr<State> next);

  // 本体の箱 1 個（砦の中の中心・半分の大きさ m）。どれもロックの陰になる
  struct BodyBox {
    Vector3 center;
    Vector3 halfSize;
  };

  /// <summary>
  /// 道筋の点の番号 u の位置（Catmull-Rom）
  /// 両端の外側には、端の 2 点を結ぶ向きにまっすぐ延ばした点を置き、端でも速さが落ちないようにする
  /// 最後の点を過ぎた u は、最後の 2 点を結ぶ向きにまっすぐ延ばした所にする
  /// </summary>
  Vector3 PathPoint(float u) const;

  /// <summary>
  /// 位置・向き・傾きから frame_ と箱の行列を作る
  /// </summary>
  void BuildFrame();

  /// <summary>
  /// 道筋をなぞる。道筋の終わりを過ぎて、残った部品と一緒に消えたら true
  /// </summary>
  bool UpdateFly();

  /// <summary>
  /// 部品を全部壊されてから耐える間の動き。割れ始めるときが来たら true
  /// </summary>
  bool UpdateEnduring();

  /// <summary>
  /// 耐え始める（倒されたときの傾き・道筋の位置を覚え、倒されたことを知らせる）
  /// </summary>
  /// <param name="lastPartPosition">最後に壊れた部品の位置（砦の中の座標）</param>
  void BeginEnduring(const Vector3 &lastPartPosition);

  /// <summary>
  /// カメラの頭上に来たら（レールの前の向きの距離が kPassOverDistance_ より近づいたら）、頭上を過ぎたことにする
  /// </summary>
  void CheckPassOver();
  /// <summary>
  /// 割れ目が走ってからの動きと死亡演出。死亡演出が終わったら true
  /// </summary>
  bool UpdateDying();

  /// <summary>
  /// 倒されたあとの 1 フレーム分、カメラの t ではなく自分の時計で道筋を進む。向きは動く向きへだんだん回し、傾きはふらつかせる
  /// 割れ始めてからは、推力を失って進む速さが減り、重力で落ち、落ちる向きへ傾き、割れ目が走る間は小刻みに震える
  /// 行列と死亡演出に渡す箱を作り直す
  /// </summary>
  void FollowPathAfterDeath();

  /// <summary>
  /// 倒されたあとの道筋の点の番号 u と、道筋の上の高さ height を dt 秒進める
  /// 水平の速さは、道筋自身の速さに推力 thrust を掛けたもの。上限 deadSpeedLimit_ を決めたあとは、それより速くしない
  /// 高さは、頭上を過ぎる前は道筋の高さに乗る。過ぎてからは、道筋が今の高さより下る所だけ下げ、上る所では今の高さを保つ
  /// </summary>
  void AdvanceDeadPath(float &u, float &height, float thrust, float dt) const;

  /// <summary>
  /// 死亡演出を見せられる位置か（中心が画面のふちより内側で、カメラから前へ十分に離れている）
  /// </summary>
  bool IsInDeathView() const;

  /// <summary>
  /// 砦の一部でもカメラの画面に映っているか（中心から kScreenExitHalf_ の箱の角と中心のどれかが画面の中にあるか）
  /// </summary>
  bool IsOnScreen() const;

  /// <summary>
  /// 今割れ始めたら、割れ目が走って大爆発するまでの間、本体がカメラから kDeathMinClearance_ 以上離れていられるか
  /// 毎フレームの動きと同じ計算で、推力を失いながら道筋を進み、重力で落ちる、として先読みする
  /// </summary>
  bool KeepsClearOfCamera() const;

  void UpdateTilt();

  /// <summary>
  /// 死亡演出を始める。本体を塊に置き換え、最後に壊れた部品の位置に一番近い本体の箱の表面の点から割れ目を走らせる
  /// </summary>
  void StartDeath(const Vector3 &lastPartPosition);

  /// <summary>
  /// 推進器が火を噴く（速く動いている間）
  /// </summary>
  void EmitThrusterFlames();

  /// <summary>
  /// 推進器 1 個の後ろから火を出す
  /// </summary>
  void EmitThrusterFlame(int index);

  /// <summary>
  /// 本体の箱ごとに、ボロノイで割った塊を今の位置・向きに並べてつかんでおく（倒されたとき 1 回）
  /// </summary>
  void BreakIntoChunks();

  /// <summary>
  /// 本体の箱を今の位置で、つかんでいる塊と一緒に、死亡演出に渡す箱にする
  /// </summary>
  void BuildDeathBoxes();

  /// <summary>
  /// つかんでいた塊を放して、砦の今の速さを引き継いで飛ばす（大爆発のとき 1 回）
  /// </summary>
  void ReleaseChunks();

  static constexpr float kFrameTime_ = 1.0f / 60.0f;        // 1 フレームの秒数
  static constexpr float kPathTPerSecond_ = 0.2f;           // 道筋の秒をレールの t に直す値（RailCamera のふだんの速さ。1 秒に進む区間の数）
  static constexpr float kHeadingStep_ = 0.01f;             // 進む向きを決める、道筋の上の前後の点の間（点の番号）
  static constexpr float kBalloonRoll_ = 0.10f;             // 気嚢 1 個が壊れるたびに、その左右の側へ傾く角度（ラジアン）
  static constexpr float kBalloonPitch_ = 0.06f;            // 気嚢 1 個が壊れるたびに、その前後の側へ傾く角度（ラジアン）
  static constexpr float kTiltFollow_ = 0.05f;              // 傾きが目標へ近づく割合（1 フレーム）
  // 割れ始めてよい、カメラから前への距離（m）。本体（長さ 40 m）が画面の横の約 3 割に映る
  static constexpr float kDeathViewDistance_ = 90.0f;
  static constexpr float kDeathViewMarginUV_ = 0.1f;       // 割れ始めてよい、画面のふちからの内側の幅（画面の大きさに対する割合）
  // 道筋の終わりを過ぎたあとの動き
  static constexpr float kScreenExitHalf_ = 32.0f;         // 画面に映っているかを調べる、砦の中心からの箱の半分の大きさ（m）。気嚢の上の端まで入る
  static constexpr float kMaxOverrunT_ = 1.2f;             // 道筋の終わりを過ぎてから、画面に映っていても消すまでのレールの t
  // 倒されたあとの動き
  static constexpr float kDeadTurnFollow_ = 0.03f;         // 向きが動く向きへ近づく割合（1 フレーム）
  static constexpr float kDeadTurnMinSpeed_ = 5.0f;        // 横の速さがこれより遅い間は向きを変えない（m/秒）
  static constexpr float kDeadSpeedMin_ = 50.0f;           // 倒されたあとに道筋を進む水平の速さの上限の、いちばん小さい値（m/秒）
  static constexpr float kDeadThrustLossRate_ = 0.7f;      // 割れ始めてから、道筋を進む速さが 1 秒に減る割合（e を底にした指数。1 秒でおよそ半分）
  static constexpr float kDeadGravity_ = 9.8f;             // 割れ始めてから落ちる加速度（m/秒²）
  static constexpr float kDeadTiltSeconds_ = 0.5f;         // 割れ始めてから、落ちる向きへ傾ききるまでの秒数
  static constexpr float kCrackShakeDistance_ = 0.8f;      // 割れ目が走りきるときの、震えの位置の大きさ（m）
  static constexpr float kCrackShakeAngle_ = 0.02f;        // 割れ目が走りきるときの、震えの傾きの大きさ（ラジアン）
  static constexpr float kCrackShakeHz_ = 13.0f;           // 震えの、1 秒あたりの往復の数
  static constexpr float kDeadWobbleRoll_ = 0.12f;         // 左右のふらつきの大きさ（ラジアン）
  static constexpr float kDeadWobbleRollHz_ = 0.6f;        // 左右のふらつきの、1 秒あたりの往復の数
  static constexpr float kDeadWobblePitch_ = 0.06f;        // 前後のふらつきの大きさ（ラジアン）
  static constexpr float kDeadWobblePitchHz_ = 0.9f;       // 前後のふらつきの、1 秒あたりの往復の数
  static constexpr float kMinEnduringSeconds_ = 1.5f;      // 見える位置にいても、頭上を過ぎてからこれだけ耐えるまでは割れ始めない（秒）
  static constexpr float kMaxEnduringSeconds_ = 5.0f;      // 見える位置に来なくても、頭上を過ぎてからこれだけ耐えたら割れ始める（秒）
  static constexpr float kDeathMinClearance_ = 60.0f;      // 割れ始めてから大爆発まで、本体とカメラの間に空けておく距離（m）
  static constexpr float kFallRoll_ = 0.5f;                 // 落ちる間に目指す左右の傾き（ラジアン）
  static constexpr float kFallPitch_ = 0.35f;               // 落ちる間に目指す前の下がり（ラジアン）
  static constexpr float kPassOverDistance_ = 10.0f;        // レールの前の向きでこの距離より近づいたら、カメラの頭上に来たとみなす（m）
  static constexpr float kThrustSpeed_ = 60.0f;            // これより速く動く間、推進器が火を噴く（m/秒）
  static constexpr int kFlameInterval_ = 3;                 // 推進器の火を出す間隔（フレーム）
  static constexpr float kFlameBack_ = 4.0f;                // 火を出す所の、推進器の中心から後ろへの距離（m）
  static constexpr float kFlameScale_ = 1.5f;               // 火の大きさの倍率

  std::vector<Vector3> path_;
  const RailCamera *railCamera_ = nullptr;
  std::vector<Event> events_; // まだ取り出されていない出来事
  float spawnT_ = 0.0f;
  float pathSpanT_ = 1.0f;             // 道筋を走り切るまでのレールの t
  std::optional<float> heading_;

  std::unique_ptr<State> state_; // 今の状態
  Vector3 position_ = {0.0f, 0.0f, 0.0f};
  Vector3 velocity_ = {0.0f, 0.0f, 0.0f}; // 1 秒に動く量（m/秒）
  float yaw_ = 0.0f;
  float roll_ = 0.0f;
  float pitch_ = 0.0f;
  float targetRoll_ = 0.0f;
  float targetPitch_ = 0.0f;
  bool hasPassedOver_ = false;        // カメラの頭上を過ぎたか
  bool hasPosition_ = false;          // 一度でも位置を決めたか
  int flameTimer_ = 0;
  float deadPathU_ = 0.0f;                          // 倒されたあとの、道筋の点の番号
  float deadPathHeight_ = 0.0f;                     // 倒されたあとの、落ちる分を引く前の高さ（m）
  bool hasDeadSpeedLimit_ = false;                  // 倒されたあとに道筋を進む水平の速さの上限を決めたか
  float deadSpeedLimit_ = kDeadSpeedMin_;           // 倒されたあとに道筋を進む水平の速さの上限（頭上を過ぎて見える位置に入った瞬間の水平の速さ。m/秒）
  float enduringTimer_ = 0.0f;                     // 頭上を過ぎてから耐えた秒数
  Vector3 lastPartPosition_ = {0.0f, 0.0f, 0.0f};  // 最後に壊れた部品の位置（砦の中の座標）
  float deadTime_ = 0.0f;                          // 倒されてからの秒数
  float fallTime_ = 0.0f;                          // 割れ始めてからの秒数
  Vector3 shakeOffset_ = {0.0f, 0.0f, 0.0f};       // 震えで本体の見た目をずらす量（m）
  float shakeRoll_ = 0.0f;                         // 震えで足す左右の傾き（ラジアン）
  float shakePitch_ = 0.0f;                        // 震えで足す前後の傾き（ラジアン）
  float deadRollBase_ = 0.0f;                      // 倒されたあとに目指す左右の傾き（ふらつきの真ん中。ラジアン）
  float deadRollStart_ = 0.0f;                     // 倒されたときの左右の傾きの目標（ラジアン）
  float deadPitchStart_ = 0.0f;                    // 倒されたときの前後の傾きの目標（ラジアン）
  float deadTiltRate_ = 0.0f;                      // 落ちる向きへ傾いた割合（0〜1）
  std::vector<int> chunkSeedCounts_;  // 本体の箱ごとの割る数（出たときの死亡演出の設定。bodyBoxes_ の順）
  Matrix4x4 frame_{};

  std::array<EnemyPart *, kPartCount> parts_{};
  int aliveParts_ = 0;

  std::vector<BodyBox> bodyBoxes_;
  BigEnemyDeath death_;                            // 死亡演出
  std::vector<BigEnemyDeath::Box> deathBoxes_;     // 死亡演出に渡す箱（毎フレーム作り直す）
  std::unique_ptr<Object3d> root_;                 // 位置と向き
  std::unique_ptr<Object3d> tilt_;                 // 傾き（root_ の子）
  std::vector<std::unique_ptr<Object3d>> boxes_;   // 本体の箱（tilt_ の子）
  std::vector<std::unique_ptr<FractureDebris>> heldChunks_; // 本体の箱を割った塊（大爆発までつかんでおく。bodyBoxes_ の順）
};
