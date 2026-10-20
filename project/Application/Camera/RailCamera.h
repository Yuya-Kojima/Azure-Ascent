#pragma once
#include "Render/Camera/ICamera.h"
#include "Math/Matrix4x4.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include <vector>

/// <summary>
/// スプライン曲線（Catmull-Rom）に沿って移動するレールカメラ
/// </summary>
class RailCamera : public ICamera {
public:
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

  // バンク（ロール）用
  float bankStrength_ = 1.5f; // 少し弱めに設定
  float currentBankAmount_ = 0.0f; // スムージング用の現在値

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
