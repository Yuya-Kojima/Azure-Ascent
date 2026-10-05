#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Render/Sprite/Sprite.h"
#include <memory>
#include <vector>
#include <array>

#include "Framework/BaseActor.h"

class SpriteRenderer;

/// <summary>
/// ロックオンを管理する専用クラス
/// </summary>
class LockOn {
public:
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 毎フレームの更新
  /// </summary>
  /// <param name="enemies">ターゲット候補となる敵のリスト</param>
  /// <param name="viewProjectionMatrix">現在のカメラのViewProjection行列</param>
  /// <param name="reticlePos">プレイヤーのメイン照準の2D座標</param>
  /// <param name="isLockOnMode">プレイヤーがロックオンボタンを長押ししているか</param>
  void Update(const std::vector<BaseActor *> &targets,
              const Matrix4x4 &viewProjectionMatrix,
              const Vector2 &reticlePos,
              bool isLockOnMode, float lockOnRadius);

  /// <summary>
  /// 2Dカーソルの描画
  /// </summary>
  void Draw();

  /// <summary>
  /// ロックオン状態の種別
  /// </summary>
  enum class TargetState {
    Locking,  // ロックオン準備中（長押し中、発射前）
    Tracking  // ホーミング弾追尾中（発射後、着弾待ち）
  };

  /// <summary>
  /// ロックオン状態を解除（すべて解除）
  /// </summary>
  void Clear();

  /// <summary>
  /// 準備中（Locking）のターゲットのみ解除（キャンセル時）
  /// </summary>
  void ClearLocking();

  /// <summary>
  /// ホーミング弾発射時の通知（Locking 状態の敵を Tracking 状態へ移行）
  /// </summary>
  void OnHomingFired();

  /// <summary>
  /// ホーミング弾着弾時の通知（指定ターゲットの追尾マーカーを解除）
  /// </summary>
  void RemoveTrackingTarget(BaseActor* target);

  const std::vector<BaseActor*>& GetTargets() const { return targets_; }

private:
  struct TargetInfo {
    BaseActor* actor = nullptr;
    float lockTime = 0.0f; // 経過時間（秒）
    TargetState state = TargetState::Locking;
  };

  static const size_t kMaxDisplayReticles = 16; // 画面上に表示できるマーカー最大数
  static const size_t kMaxLockOnPerVolley = 8;  // 1回の一斉射撃でロックオンできる最大数
  static const int kLockOnInterval = 6;        // ロックオンする間隔（フレーム、約0.10秒）
  std::vector<BaseActor *> targets_;            // 互換性・外部参照用の発射対象敵リスト
  std::vector<TargetInfo> targetInfos_;         // ロックオン・追尾演出用の詳細情報リスト
  int lockOnDelayTimer_ = 0;                    // ロックオン間隔を管理するタイマー
  
  // 照準（マーカー）用スプライト
  std::array<std::unique_ptr<Sprite>, kMaxDisplayReticles> reticles_;
  
  Matrix4x4 viewProjectionMatrix_;  // 描画用のカメラ行列キャッシュ
};
