#pragma once
#include "Render/Camera/ICamera.h"
#include "Math/Matrix4x4.h"
#include "Math/Transform.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include <vector>

/// <summary>
/// スプライン曲線（Catmull-Rom）に沿って移動するレールカメラ
/// </summary>
class RailCamera : public ICamera {
public:
  // レールの線からカメラまでの高さ（m）
  static constexpr float kRailHeight_ = 2.5f;

  RailCamera();
  ~RailCamera() override = default;

  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="waypoints">通過する座標のリスト</param>
  void Initialize(const std::vector<Vector3>& waypoints);

  /// <summary>
  /// 毎フレームの更新処理
  /// </summary>
  void Update();

  /// <summary>
  /// カメラを揺らす
  /// </summary>
  void Shake(float intensity, float duration);

  // --- ICamera インターフェースの実装 ---
  const Matrix4x4& GetViewMatrix() const override { return viewMatrix_; }
  const Matrix4x4& GetProjectionMatrix() const override { return projectionMatrix_; }
  const Matrix4x4& GetViewProjectionMatrix() const override { return viewProjectionMatrix_; }
  Vector3 GetTranslate() const override { return transform_.translate; }

  // ゲッター/セッター
  void SetSpeed(float speed) { speed_ = speed; }
  // 進行スピードに掛ける割合（開始演出で 0 から加速させる）
  void SetSpeedScale(float scale) { speedScale_ = scale; }
  // 今の進行スピード（1 秒に進む区間の数。割合を掛けた後の値で、止まっている間は 0）
  float GetCurrentSpeed() const {
    return (isFinished_ || !isAutoMove_) ? 0.0f : speed_ * speedScale_ * GetEventSpeedScale();
  }

  /// <summary>
  /// 出来事でレールの速さに掛ける倍率を、seconds 秒かけてなめらかに scale へ変える（SetSpeedScale とは別に掛かる）
  /// </summary>
  void ChangeEventSpeedScale(float scale, float seconds);
  // 出来事でレールの速さに掛けている今の倍率
  float GetEventSpeedScale() const;

  /// <summary>
  /// 画角を狭めて、少し保って、戻す（見る向きは変えない）
  /// 拡大の中心は画面の真ん中ではなく、anchor が画面に映る所（そこは動かずに、まわりが外へ広がる）
  /// </summary>
  /// <param name="anchor">拡大の中心にする所（ワールド）</param>
  /// <param name="fovScale">押し込んだときの画角の倍率（小さいほど大きく映る）</param>
  /// <param name="inSeconds">押し込む秒数</param>
  /// <param name="holdSeconds">保つ秒数</param>
  /// <param name="outSeconds">戻す秒数</param>
  void PunchZoom(const Vector3& anchor, float fovScale, float inSeconds, float holdSeconds,
                 float outSeconds);

  /// <summary>
  /// 顔の向きのキーを、レールの進み具合 holdT の所の値で止める（t がそれより先へ進んでも、その先のキーを使わない）
  /// </summary>
  void HoldLook(float holdT);

  /// <summary>
  /// 止めていた顔の向きのキーを解き、止めていた値から今の t の値へ seconds 秒かけて戻す
  /// </summary>
  void ReleaseLook(float seconds);

  bool IsLookHeld() const { return isLookHeld_; }

  /// <summary>
  /// 注視・顔の向きの止め・速さの倍率を、何もしていない状態に戻す
  /// </summary>
  void ResetEventControls();
  const Vector3& GetRotate() const { return transform_.rotate; }
  bool IsFinished() const { return isFinished_; }
  float GetT() const { return t_; }
  void SetT(float t) { 
    t_ = t; 
    if (waypoints_.size() > 0 && t_ < static_cast<float>(waypoints_.size() - 1)) {
      isFinished_ = false;
    }
  }
  float GetFov() const { return fov_; }
  void SetFov(float fov) { fov_ = fov; }

  // 自動進行フラグ
  void SetAutoMove(bool autoMove) { isAutoMove_ = autoMove; }
  bool GetAutoMove() const { return isAutoMove_; }
  const std::vector<Vector3>& GetWaypoints() const { return waypoints_; }
  std::vector<Vector3>& GetWaypointsRef() { return waypoints_; }

  /// <summary>
  /// 進行度tから現在の座標を計算する
  /// </summary>
  Vector3 CalcPosition(float t) const;

      /// <summary>
  /// 進行度tにおける接線ベクトルを計算する
  /// </summary>
  Vector3 CalcTangent(float t) const;

  // レールの基準座標・ベクトル（カメラのシェイクや首振りの影響を受けない、レールそのものの位置・向き）
  const Vector3& GetRailPosition() const { return railPos_; }
  const Vector3& GetRailForward() const { return railForward_; }
  const Vector3& GetRailRight() const { return railRight_; }
  const Vector3& GetRailUp() const { return railUp_; }

  // カメラの構図: レールに、見下ろし・注視対象・顔の向きのキーを足した位置と向き（自機についていく首振り・バンク・揺れの前）
  // 自機はこの位置の前に置かれ、right・up の向きに動く
  struct Framing {
    Vector3 position = {0.0f, 0.0f, 0.0f};
    Vector3 forward = {0.0f, 0.0f, 1.0f};
    Vector3 right = {1.0f, 0.0f, 0.0f};
    Vector3 up = {0.0f, 1.0f, 0.0f};
  };
  const Framing& GetFraming() const { return framing_; }

  /// <summary>
  /// 画面の上で、構図の前 kLookDistance_ の所から右へ x・上へ y（m）ずれて見えるように自機が立つ、構図の上の位置
  /// カメラは自機のずれの kFollowWeight の割合だけ首を振ってついていき、その分だけ画面の上のずれが縮むので、縮む分を足して返す
  /// </summary>
  Vector2 ToFramingOffset(const Vector2& viewOffset) const {
    return {viewOffset.x / (1.0f - kFollowWeightX_), viewOffset.y / (1.0f - kFollowWeightY_)};
  }

private:
  Transform transform_;
  Matrix4x4 worldMatrix_;
  Matrix4x4 viewMatrix_;
  Matrix4x4 projectionMatrix_;
  Matrix4x4 viewProjectionMatrix_;

  float fov_;
  float aspectRatio_;
  float nearClip_;
  float farClip_;

public:
  // カメラの顔の向きのキー（レールの進み具合 t ごとに、レールの向きからどれだけ振るか）
  struct LookKey {
    float t = 0.0f;    // レールの進み具合
    float yaw = 0.0f;  // 見る向きをレールの上の軸まわりに振る角度（度、左が負）
    float roll = 0.0f; // 見る向きの軸まわりに傾ける角度（度）
    float out = 0.0f;  // カメラをレールの右へずらす距離（m）
  };

  // キーを t の順に並べて設定する
  void SetLookKeys(const std::vector<LookKey>& keys);
  const std::vector<LookKey>& GetLookKeys() const { return lookKeys_; }
  std::vector<LookKey>& GetLookKeysRef() { return lookKeys_; }
  // キーを t の順に並べ直す（エディタで t を書き換えた後に呼ぶ）
  void SortLookKeys();
  // t の位置の顔の向き。キーの間は smoothstep でつなぎ、最初のキーより前と最後のキーより後は 0
  LookKey SampleLook(float t) const;
  // 今の顔の向き（止め・解いた後の戻りを含む）
  LookKey CurrentLook() const;

private:
  std::vector<LookKey> lookKeys_;

  // 顔の向きのキーの止め
  bool isLookHeld_ = false;
  float lookHoldT_ = 0.0f;
  LookKey lookReleaseFrom_;          // 解いたときの顔の向き
  float lookReleaseTimer_ = 0.0f;    // 解いてからの秒数
  float lookReleaseSeconds_ = 0.0f;  // 今の t の値へ戻し切る秒数（0 なら戻っている）

  // 画角を狭めて、保って、戻す
  Vector3 zoomPunchAnchor_ = {0.0f, 0.0f, 0.0f};
  float zoomPunchFovScale_ = 1.0f;
  float zoomPunchIn_ = 0.0f;
  float zoomPunchHold_ = 0.0f;
  float zoomPunchOut_ = 0.0f;
  float zoomPunchTimer_ = -1.0f; // 押し込み始めてからの秒数（負なら押し込んでいない）
  static constexpr float kMinZoomPunchFovScale_ = 0.2f; // 画角の倍率の下限
  // 今の画角に掛ける倍率
  float ZoomPunchFovScale() const;
  // projectionMatrix_ に、拡大の中心を動かさない拡大を掛ける
  void ApplyZoomPunch();

  // 出来事でレールの速さに掛ける倍率
  float eventSpeedFrom_ = 1.0f;
  float eventSpeedTo_ = 1.0f;
  float eventSpeedTimer_ = 0.0f;
  float eventSpeedSeconds_ = 0.0f;

  // レール移動用
  std::vector<Vector3> waypoints_; // 通過ポイント
  float t_ = 0.0f; // 現在の進行度
  float speed_ = 0.5f; // 進行スピード
  float speedScale_ = 1.0f; // 進行スピードに掛ける割合
  bool isFinished_ = false;
  bool isAutoMove_ = true; // 自動で前進するかどうか

  // 画面揺れ用
  float shakeIntensity_ = 0.0f;
  float shakeDuration_ = 0.0f;
  float shakeTimer_ = 0.0f;

  // 構図と、自機についていく首振り・バンク用
  static constexpr float kLookDistance_ = 10.0f;  // 注視点までの距離（m）
  static constexpr float kLookDown_ = 1.31f;      // 構図の注視点を下げる量（m）。照準が真ん中のときは自機の高さへの首振りと合わせて 10m 先で約 2m 下（約 11°）を見る
  static constexpr float kFollowWeightX_ = 0.40f; // 構図の上の自機の左右のずれ 1m あたりに注視点を左右へ引く量（m）
  static constexpr float kFollowWeightY_ = 0.25f; // 構図の上の自機の上下のずれ 1m あたりに注視点を上下へ引く量（m）
  static constexpr float kBankScale_ = -0.02f;    // 自機の左右のずれ × bankStrength_ を、上の向きを右へ倒す量に直す値
  static constexpr float kBankFollow_ = 0.03f;    // バンクが目標へ近づく割合（1 フレーム）
  float bankStrength_ = 1.5f;      // バンクの強さ
  float currentBankAmount_ = 0.0f; // スムージング用の現在値
  Framing framing_;

  // プレイヤー連動用
  Vector3 playerWorldPos_ = {0.0f, 0.0f, 0.0f};

  Vector3 railPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 railForward_ = {0.0f, 0.0f, 1.0f};
  Vector3 railRight_ = {1.0f, 0.0f, 0.0f};
  Vector3 railUp_ = {0.0f, 1.0f, 0.0f};

public:
  void SetPlayerWorldPosition(const Vector3& pos) { playerWorldPos_ = pos; }
  
  // ボスなどの特定の対象を注視するための機能
  void SetFocusTarget(const Vector3* target) { focusTarget_ = target; }
  void SetFocusWeight(float weight) { focusWeight_ = weight; }

private:
  const Vector3* focusTarget_ = nullptr;
  float focusWeight_ = 0.0f;
};
