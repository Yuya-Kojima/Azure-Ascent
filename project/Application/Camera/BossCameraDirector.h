#pragma once
#include "Actor/Boss.h"
#include <vector>

class RailCamera;

/// <summary>
/// ボスの出来事に合わせて、カメラを揺らし、形態変化の間はボスへ寄せて戻す
/// 形態変化が始まったら画角を狭めながらボスを注視し、終わったら少し保ってから画角と注視を戻す
/// </summary>
class BossCameraDirector {
public:
  /// <summary>
  /// ボスの出来事 1 つを受けて、カメラを揺らし、寄る・戻す流れを始める
  /// </summary>
  void OnEvent(Boss::CameraEvent event, RailCamera &railCamera);

  /// <summary>
  /// 寄る・戻す流れを 1 フレーム進めて、画角と注視をカメラへ渡す
  /// ボスがいなくなったら（boss が nullptr）、画角と注視をすぐ戻して流れを終える
  /// </summary>
  void Update(const Boss *boss, RailCamera &railCamera);

  /// <summary>
  /// 流れを止めて、何もしていない状態に戻す（カメラには触らない）
  /// </summary>
  void Reset();

private:
  // 寄る・戻す流れ
  enum class Flow {
    None,     // 何もしていない
    ZoomIn,   // 形態変化の間、ボスへ寄せる
    PullBack, // 形態変化の後、保ってから戻す
  };

  // 画角と注視を初期値へ戻して流れを終える
  void Finish(RailCamera &railCamera);

  Flow flow_ = Flow::None;
  float flowTimer_ = 0.0f; // 今の流れが始まってからの秒数

  // 出来事ごとの揺れ（強さ・秒）
  static constexpr float kCounterShakeIntensity_ = 1.5f;    // カウンター成功
  static constexpr float kCounterShakeSeconds_ = 0.5f;
  static constexpr float kBarrageShakeIntensity_ = 0.3f;    // 弾をばらまいた
  static constexpr float kBarrageShakeSeconds_ = 0.1f;
  static constexpr float kMissileShakeIntensity_ = 0.5f;    // ミサイルを出した
  static constexpr float kMissileShakeSeconds_ = 0.15f;
  static constexpr float kDashShakeIntensity_ = 0.8f;       // 突進を始めた
  static constexpr float kDashShakeSeconds_ = 0.3f;
  static constexpr float kTransitionEndShakeIntensity_ = 3.0f; // 形態変化が終わった
  static constexpr float kTransitionEndShakeSeconds_ = 0.5f;

  // 寄る間の揺れ。強さは形態変化の進み（0〜1）に掛ける
  static constexpr float kZoomInShakeMaxIntensity_ = 0.5f;
  static constexpr float kZoomInShakeSeconds_ = 0.1f;

  // 画角と秒
  static constexpr float kZoomFov_ = 25.0f * (3.14159265f / 180.0f); // 寄ったときの画角（ラジアン）
  static constexpr float kZoomInSeconds_ = 1.0f;   // 寄り切るまでの秒数
  static constexpr float kHoldSeconds_ = 1.0f;     // 形態変化の後、寄ったまま保つ秒数
  static constexpr float kPullBackSeconds_ = 1.0f; // 保った後、戻し切るまでの秒数
};
