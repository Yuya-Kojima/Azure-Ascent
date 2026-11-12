#pragma once
#include "Actor/EnemyPart.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <array>
#include <memory>
#include <vector>

class InterceptCue;
class Object3d;
class Object3dRenderer;
class Player;
class RailCamera;

/// <summary>
/// 大蛇の 1 回の出番の決まり（レベルの出し方に書く）
/// </summary>
struct SerpentSpec {
  std::vector<int> coreSegments;          // 溜めの間だけ光って撃てる節の番号（0 が頭）
  float stopSeconds = 0.0f;               // 道筋の初めから、止まって溜める所までの秒数
  float chargeSeconds = 0.0f;             // 溜めの秒数（これが過ぎると電撃）
  float appearSeconds = 0.0f;             // 道筋の初めから、頭が壁の穴から出るまでの秒数
  int zapMax = 0;                         // 電撃 1 回のダメージの上限（0 なら残った光る節の数のまま）
  bool wallZap = false;                   // 電撃を自機でなく壁に走らせる（予告。撃てる節がない）
  bool isFinal = false;                   // 最後の見せ場の出番（光る節を全部撃つと倒れる）
  float warnSeconds = 0.0f;               // 道筋をなぞり始めてから出るまでの前触れの秒数（0 なら前触れなし）
  Vector3 warnPoint = {0.0f, 0.0f, 0.0f}; // 前触れの灯りの中心（ワールド。出る所の壁の穴）
  float warnYaw = 0.0f;                   // 前触れの灯りの向き（y 回転のラジアン。壁の外向き）
};

/// <summary>
/// 区間 3 の機械の大蛇の 1 回の出番
/// 壁の穴から道筋を這って来て、止まる所で止まって電気を溜める。溜めの間だけ光る節が撃てる
/// 光る節を全部撃てば溜めが止まり、撃ち切れなければ残った節から自機へ電撃が走る。そのあと道筋の残りを這って壁の穴へ潜る
/// 最後の見せ場の出番は、光る節を全部撃つと、その場で揺れて電気をまとい、尾から頭へ順に爆発する
/// 体の節は、頭が通った道筋の上に一定の間隔で並ぶ
/// </summary>
class MechSerpent : public IEnemyPartOwner {
public:
  static constexpr int kSegmentCount = 10; // 頭 + 体の節 9 個

  // 外へ知らせる出来事
  enum class Event {
    Appeared,      // 頭が壁の穴から出た
    ChargeStarted, // 止まって溜め始めた
    Ended          // 出番が終わった（最後の見せ場で倒されて爆発し終えた・電撃のあと去り始めた）
  };

  // 部品 1 個の決まり（付く節・役・節の中の位置・半分の大きさ・色）。節の中の座標は x 右・y 外（カメラの側）・z 前
  struct PartSpec {
    int segment;
    EnemyPart::Role role;
    Vector3 localPosition;
    Vector3 halfSize;
    Vector4 color;
  };

  MechSerpent();
  ~MechSerpent() override;

  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="renderer">箱を描く描画係</param>
  /// <param name="railCamera">進み具合 t を読み、前触れ・爆発で揺らすカメラ</param>
  /// <param name="player">電撃を当てる自機</param>
  /// <param name="cue">迎撃の印を描く係</param>
  /// <param name="path">頭のワールドの道筋の点（点と点の間は同じ秒数）</param>
  /// <param name="spawnT">出たときのレールの進み具合</param>
  /// <param name="pathSeconds">道筋を走り切る秒数（止まる間は入らない）</param>
  /// <param name="spec">この出番の決まり</param>
  void Initialize(Object3dRenderer *renderer, RailCamera *railCamera, Player *player,
                  InterceptCue *cue, const std::vector<Vector3> &path, float spawnT,
                  float pathSeconds, const SerpentSpec &spec);

  /// <summary>
  /// 部品の決まり（番号の順）
  /// </summary>
  const std::vector<PartSpec> &GetPartSpecs() const { return partSpecs_; }

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

  const Vector3 &GetHeadPosition() const { return segmentPositions_[0]; }

  /// <summary>
  /// 光る節の核の真ん中（ワールド）。光る節がなければ頭の位置
  /// </summary>
  Vector3 GetCoreCenter() const;
  bool IsFinal() const { return spec_.isFinal; }
  // 予告（電撃を壁に走らせる、撃てない出番）
  bool IsPreview() const { return spec_.wallZap; }

  /// <summary>
  /// 壁の穴へ潜り終えた・爆発し終えた
  /// </summary>
  bool IsFinished() const { return state_ == State::Finished; }

  // IEnemyPartOwner
  const Matrix4x4 &GetPartFrame(int index) const override;
  Vector3 GetPartRotation(int index) const override;
  bool CanPartFire() const override { return false; }
  void OnPartDestroyed(int index) override;
  bool IsPartActive(int index) const override;

private:
  enum class State {
    Approach, // 止まる所まで這う
    Charge,   // 止まって溜める
    Result,   // 溜めが止まった・電撃を撃った直後の間
    Leave,    // 道筋の残りを這って壁の穴へ潜る
    Dying,    // 最後の見せ場で倒された:揺れて電気をまとい、尾から頭へ爆発する
    Finished  // 終わり
  };

  /// <summary>
  /// 道筋の点の番号 u の位置（Catmull-Rom）
  /// </summary>
  Vector3 PathPoint(float u) const;

  /// <summary>
  /// 道筋の初めからの長さ（m）の所の、道筋の点の番号
  /// </summary>
  float IndexAtLength(float length) const;

  /// <summary>
  /// 道筋の初めからの秒数の所の、頭の道筋の上の長さ（m）。道筋の終わりを過ぎたら、終わりの速さのまま奥へ進んだとして数える
  /// </summary>
  float HeadLength(float seconds) const;

  void UpdateApproach();
  void UpdateCharge();
  void UpdateResult();
  void UpdateLeave();
  void UpdateDying();
  void UpdateWarn();

  void StartCharge();
  void FireZap();
  void StartDying();

  /// <summary>
  /// 頭の長さから節の位置と前の向きを決める
  /// </summary>
  void PlaceSegments();

  /// <summary>
  /// 節の位置から、各節の向き（行列とオイラー角）を作り、箱を置く
  /// </summary>
  void BuildSegments();

  /// <summary>
  /// 光る節の色と大きさを、溜まり具合と迎撃の合図の拍に合わせ、迎撃の印に位置を渡す
  /// </summary>
  void UpdateGlow();

  /// <summary>
  /// 部品 index の核のワールドの位置
  /// </summary>
  Vector3 CorePosition(int index) const;

  /// <summary>
  /// 残っている部品と迎撃の印を静かに消す（撃破に数えない）
  /// </summary>
  void VanishParts();

  static constexpr float kFrameTime_ = 1.0f / 60.0f;       // 1 フレームの秒数
  static constexpr float kPathTPerSecond_ = 0.2f;          // 道筋の秒をレールの t に直す値（RailCamera のふだんの速さ。1 秒に進む区間の数）
  static constexpr float kSegmentSpacing_ = 17.0f;         // 隣り合う節の、道筋の上の間（m）
  static constexpr int kArcSamplesPerPoint_ = 8;           // 道筋の長さの表を作るときの、点 1 つあたりの区切りの数
  static constexpr float kLeaveRate_ = 2.0f;               // 去るときに道筋を進む速さの倍率
  static constexpr float kHeadHalf_ = 7.0f;                // 頭の箱の半分の大きさ（m）
  static constexpr float kSegmentHalf_ = 5.0f;             // 体の節の箱の半分の大きさ（m）
  static constexpr float kCoreHalf_ = 3.5f;                // 光る節の核（体の節の外側に突き出た箱）の半分の大きさ（m）
  static constexpr float kCoreSink_ = 1.0f;                // 核を体の節の外側の面に埋める深さ（m）
  static constexpr float kIdlePulseSpeed_ = 6.0f;          // 溜める前の核が脈打つ速さ（ラジアン/秒）
  static constexpr float kHeadingStep_ = 0.01f;            // 頭の向きを決める、道筋の上の前後の点の間（点の番号）
  static constexpr float kGlowLead_ = 0.4f;                // 光る節が頭から順に強まり始める、溜めの割合の幅（尾の側の節ほど遅れる）
  static constexpr float kGlowRamp_ = 0.6f;                // 光る節 1 個が弱い光から強い光になるまでの、溜めの割合
  static constexpr float kChargeFlashSeconds_ = 0.2f;      // 溜め始めに光る節が白く光る秒数
  static constexpr float kChargeSparkScale_ = 1.5f;        // 溜め始めに光る節から散る火花の大きさの倍率
  static constexpr float kCoreBeatScale_ = 0.25f;          // 拍の瞬間に光る節が膨らむ量（元の大きさに対する割合）
  static constexpr int kDamagePerLine_ = 1;                // 電撃 1 本のダメージ
  static constexpr float kResultSeconds_ = 0.5f;           // 溜めが止まった・電撃を撃ったあと、去り始めるまでの秒数
  static constexpr float kWallZapLength_ = 90.0f;          // 予告の電撃を壁に走らせる長さ（m、頭から真下へ）
  static constexpr float kFlinchSeconds_ = 0.5f;           // 溜めを止められてから体が揺れる秒数
  static constexpr float kFlinchAmplitude_ = 3.0f;         // 体が揺れる幅（m、節の外の向き）
  static constexpr float kFlinchFrequency_ = 30.0f;        // 体が揺れる速さ（ラジアン/秒）
  static constexpr float kFlinchWave_ = 0.8f;              // 隣の節との揺れのずれ（ラジアン）。頭から尾へ波が伝わる
  static constexpr float kDyingLead_ = 0.6f;               // 倒されてから、尾が初めに爆発するまでの秒数
  static constexpr float kDyingStep_ = 0.2f;               // 隣の節が爆発するまでの間（秒）
  static constexpr float kDyingTail_ = 0.4f;               // 頭が爆発してから終わるまでの秒数
  static constexpr float kDyingShake_ = 1.2f;              // 倒された体ががたがた揺れる幅（m）
  static constexpr float kDyingSparkInterval_ = 0.06f;     // 倒された体に電気の火花を出す間隔（秒）
  static constexpr float kDyingSparkScale_ = 2.5f;         // 電気の火花の大きさの倍率
  static constexpr float kExplosionScale_ = 3.0f;          // 節 1 個の爆発の大きさの倍率
  static constexpr float kHeadExplosionScale_ = 5.0f;      // 頭の爆発の大きさの倍率
  static constexpr float kExplosionShake_ = 0.35f;         // 節 1 個の爆発でカメラを揺らす強さ
  static constexpr float kHeadExplosionShake_ = 0.8f;      // 頭の爆発でカメラを揺らす強さ
  static constexpr float kExplosionShakeSeconds_ = 0.3f;   // 爆発でカメラを揺らす秒数
  static constexpr float kWarnLampHalfX_ = 7.0f;           // 前触れの灯りの横の半分の大きさ（m）
  static constexpr float kWarnLampHalfY_ = 7.0f;           // 前触れの灯りの縦の半分の大きさ（m）
  static constexpr float kWarnLampHalfZ_ = 0.8f;           // 前触れの灯りの厚みの半分（m）
  static constexpr float kWarnLampOut_ = 1.2f;             // 前触れの灯りを壁の穴の中心から外へ出す距離（m）
  static constexpr float kWarnPulseSpeed_ = 12.0f;         // 前触れの灯りが明滅する速さ（ラジアン/秒）
  static constexpr float kWarnLinger_ = 0.4f;              // 出てからも灯りを残す秒数
  static constexpr float kWarnShakeInterval_ = 0.25f;      // 前触れの間にカメラを揺らす間隔（秒）
  static constexpr float kWarnShakeIntensity_ = 0.2f;      // 前触れの揺れの強さ
  static constexpr float kWarnShakeSeconds_ = 0.3f;        // 前触れの揺れ 1 回の秒数

  std::vector<Vector3> path_;
  std::vector<float> arcIndex_;  // 長さの表の、道筋の点の番号
  std::vector<float> arcLength_; // 長さの表の、道筋の初めからの長さ（m）
  float pathSeconds_ = 1.0f;
  RailCamera *railCamera_ = nullptr;
  Player *player_ = nullptr;
  InterceptCue *cue_ = nullptr;
  float headSeconds_ = 0.0f;   // 頭が道筋の初めから進んだ秒数（止まる間は進まない）
  float lastRailT_ = 0.0f;
  State state_ = State::Approach;
  float stateTimer_ = 0.0f;    // 今の状態になってからの秒数
  bool hasAppeared_ = false;
  std::vector<Event> events_;

  SerpentSpec spec_;
  std::vector<PartSpec> partSpecs_;
  std::vector<EnemyPart *> parts_;
  std::vector<int> cueIds_;    // 部品ごとの迎撃の印の番号（0 なら印なし）
  float chargeFlashTimer_ = 0.0f;    // 溜め始めの白い光の残りの秒数
  int aliveCores_ = 0;

  std::array<Vector3, kSegmentCount> segmentPositions_{};
  std::array<Vector3, kSegmentCount> segmentForwards_{};
  std::array<Matrix4x4, kSegmentCount> segmentFrames_{};
  std::array<Vector3, kSegmentCount> segmentRotations_{};
  int explodedCount_ = 0;      // 倒された後に爆発し終えた節の数（尾から数える）

  float flinchTimer_ = 0.0f;   // 体が揺れる残りの秒数
  float flinchClock_ = 0.0f;   // 揺れの波の時刻（秒）
  float sparkTimer_ = 0.0f;
  float warnShakeTimer_ = 0.0f;
  float warnClock_ = 0.0f;     // 前触れの明滅の時刻（秒）
  float idleClock_ = 0.0f;     // 溜める前の核が脈打つ時刻（秒）

  std::array<std::unique_ptr<Object3d>, kSegmentCount> segments_;
  std::unique_ptr<Object3d> warnLamp_;
};
