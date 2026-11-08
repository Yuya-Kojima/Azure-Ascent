#pragma once
#include "Actor/EnemyPart.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <array>
#include <memory>
#include <optional>
#include <vector>

class Object3d;
class Object3dRenderer;
class RailCamera;

/// <summary>
/// 浮かぶ砦。ワールドの道筋を、レールの進み具合 t に合わせてなぞる
/// 部品 8 個を持ち、全部壊れたら傾いて雲海へ落ちて爆発し、道筋の終わりまで残ったら部品ごと消える
/// 本体はロックの対象ではなく、本体・甲板・砲座の台の箱がロックの陰になる
/// </summary>
class FloatingFortress : public IEnemyPartOwner {
public:
  static constexpr int kPartCount = 8;

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
  /// <param name="railCamera">進み具合 t を読み、爆発で揺らすカメラ</param>
  /// <param name="path">ワールドの道筋の点（点と点の間は同じ秒数）</param>
  /// <param name="spawnT">出たときのレールの進み具合</param>
  /// <param name="pathSeconds">道筋を走り切る秒数</param>
  /// <param name="heading">向き（y 回転のラジアン）。なければ進む向きを向く</param>
  void Initialize(Object3dRenderer *renderer, RailCamera *railCamera,
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
  /// ロックの陰にする箱を足す（ワールド → 箱の中の座標で箱が -1〜1 になる行列）
  /// </summary>
  void AppendOccluders(std::vector<Matrix4x4> &worldToBoxes) const;

  /// <summary>
  /// 落ち切った・去り終わった
  /// </summary>
  bool IsFinished() const { return state_ == State::Finished; }

  // IEnemyPartOwner
  const Matrix4x4 &GetPartFrame() const override { return frame_; }
  Vector3 GetPartRotation() const override;
  bool CanPartFire() const override;
  void OnPartDestroyed(int index) override;

private:
  enum class State {
    Fly,     // 道筋をなぞる
    Fall,    // 部品を全部壊されて雲海へ落ちる
    Sink,    // 雲海に沈む
    Finished // 終わり
  };

  // 本体の箱 1 個（砦の中の中心・半分の大きさ m）。どれもロックの陰になる
  struct BodyBox {
    Vector3 center;
    Vector3 halfSize;
  };

  /// <summary>
  /// 道筋の点の番号 u の位置（Catmull-Rom）
  /// </summary>
  Vector3 PathPoint(float u) const;

  /// <summary>
  /// 位置・向き・傾きから frame_ と箱の行列を作る
  /// </summary>
  void BuildFrame();

  void UpdateFly();
  void UpdateFall();
  void UpdateSink();
  void UpdateTilt();

  /// <summary>
  /// 雲海に沈む所の大きな爆発
  /// </summary>
  void Explode();

  /// <summary>
  /// 推進器が火を噴く（速く動いている間）
  /// </summary>
  void EmitThrusterFlames();

  static constexpr float kFrameTime_ = 1.0f / 60.0f;        // 1 フレームの秒数
  static constexpr float kPathTPerSecond_ = 0.2f;           // 道筋の秒をレールの t に直す値（RailCamera のふだんの速さ。1 秒に進む区間の数）
  static constexpr float kHeadingStep_ = 0.01f;             // 進む向きを決める、道筋の上の前後の点の間（点の番号）
  static constexpr float kBalloonRoll_ = 0.10f;             // 気嚢 1 個が壊れるたびに、その左右の側へ傾く角度（ラジアン）
  static constexpr float kBalloonPitch_ = 0.06f;            // 気嚢 1 個が壊れるたびに、その前後の側へ傾く角度（ラジアン）
  static constexpr float kTiltFollow_ = 0.05f;              // 傾きが目標へ近づく割合（1 フレーム）
  static constexpr float kFallGravity_ = 30.0f;             // 落ちる加速度（m/秒²）
  static constexpr float kFallDrag_ = 0.98f;                // 落ちる間に横の速さを残す割合（1 フレーム）
  static constexpr float kFallRoll_ = 0.5f;                 // 落ちる間に目指す左右の傾き（ラジアン）
  static constexpr float kFallPitch_ = 0.35f;               // 落ちる間に目指す前の下がり（ラジアン）
  static constexpr float kCloudSeaY_ = -15.0f;              // 雲海の高さ（CloudSeaConfig の height）
  static constexpr float kSinkSeconds_ = 1.5f;              // 爆発してから沈み終わるまでの秒数
  static constexpr int kExplosionCount_ = 6;                // 爆発の数
  static constexpr float kExplosionSpread_ = 18.0f;         // 爆発を散らす範囲（m）
  static constexpr float kExplosionScale_ = 5.0f;           // 爆発 1 個の大きさの倍率
  static constexpr float kShakeIntensity_ = 0.8f;           // 爆発でカメラを揺らす強さ
  static constexpr float kShakeSeconds_ = 0.5f;             // 爆発でカメラを揺らす秒数
  static constexpr float kPassOverDistance_ = 10.0f;        // レールの前の向きでこの距離より近づいたら、カメラの頭上に来たとみなす（m）
  static constexpr float kThrustSpeed_ = 60.0f;            // これより速く動く間、推進器が火を噴く（m/秒）
  static constexpr int kFlameInterval_ = 3;                 // 推進器の火を出す間隔（フレーム）
  static constexpr float kFlameBack_ = 4.0f;                // 火を出す所の、推進器の中心から後ろへの距離（m）
  static constexpr float kFlameScale_ = 1.5f;               // 火の大きさの倍率

  std::vector<Vector3> path_;
  RailCamera *railCamera_ = nullptr;
  float spawnT_ = 0.0f;
  float pathSpanT_ = 1.0f;             // 道筋を走り切るまでのレールの t
  std::optional<float> heading_;

  State state_ = State::Fly;
  Vector3 position_ = {0.0f, 0.0f, 0.0f};
  Vector3 velocity_ = {0.0f, 0.0f, 0.0f}; // 1 秒に動く量（m/秒）
  float yaw_ = 0.0f;
  float roll_ = 0.0f;
  float pitch_ = 0.0f;
  float targetRoll_ = 0.0f;
  float targetPitch_ = 0.0f;
  bool hasPassedOver_ = false;        // カメラの頭上を過ぎたか
  bool hasPosition_ = false;          // 一度でも位置を決めたか
  float sinkTimer_ = 0.0f;
  int flameTimer_ = 0;
  Matrix4x4 frame_{};

  std::array<EnemyPart *, kPartCount> parts_{};
  int aliveParts_ = 0;

  std::vector<BodyBox> bodyBoxes_;
  std::unique_ptr<Object3d> root_;                 // 位置と向き
  std::unique_ptr<Object3d> tilt_;                 // 傾き（root_ の子）
  std::vector<std::unique_ptr<Object3d>> boxes_;   // 本体の箱（tilt_ の子）
};
