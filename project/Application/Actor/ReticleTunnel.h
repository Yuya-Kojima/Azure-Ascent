#pragma once
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Matrix4x4.h"
#include "Render/Sprite/Sprite.h"
#include <memory>
#include <array>
#include <numbers>

class SpriteRenderer;
class ICamera;
class Input;

/// <summary>
/// パンツァードラグーン風 3D視線トンネル照準クラス
/// 手前の角かぎ・中の四角・奥の四角と中心点を、角どうしの接続線で筒状につなぐ
/// </summary>
class ReticleTunnel {
public:
  struct Config {
    // 2D照準移動パラメータ（操作感）
    float reticleMaxSpeed = 25.0f;     // 照準の最高移動速度

    // 3D視線トンネルパラメータ（奥行き・サイズ）
    float nearDistance = 35.0f;        // 手前枠の3D奥行き距離
    float midDistance = 70.0f;         // 中間枠の3D奥行き距離
    float farDistance = 120.0f;        // 奥枠の3D奥行き距離（カメラから照準方向へ。手前・中の枠はこれに対する割合で並ぶ）

    float nearSize = 3.5f;             // 手前枠の3D基本サイズ（ロックオン範囲に合わせて拡大）
    float midSize = 3.0f;              // 中間枠の3D基本サイズ
    float farSize = 3.2f;              // 奥枠の3D基本サイズ

    float originCameraBlend = 0.35f;   // トンネルの起点（0=自機、1=カメラ）。カメラ寄りほど枠が同じ中心に並ぶ

    float lockOnSpinSpeed = 6.0f;      // ロックオン中にトンネル全体が回る速さ（ラジアン/秒）

    float lineThickness = 2.5f;        // 枠線の太さ（ピクセル）
    float targetDistance = 1000.0f;    // 射撃目標までの基本距離
  };

  ReticleTunnel() = default;
  ~ReticleTunnel() = default;

  /// <summary>
  /// 初期化処理
  /// </summary>
  void Initialize(SpriteRenderer* spriteRenderer);

  /// <summary>
  /// 毎フレームの更新処理（入力操作・3D視線計算・頂点投影）
  /// </summary>
  void Update(Input* input, const ICamera* camera, const Vector3& playerPos, bool isLockOn);

  /// <summary>
  /// 2Dトンネル枠線スプライトの描画
  /// </summary>
  void Draw();

  /// <summary>
  /// 照準座標を画面中央にリセット
  /// </summary>
  void ResetPosition();

  // プレイヤーおよび武器システム用のアクセサ
  const Vector2& Get2DPosition() const { return position_; }
  const Vector3& GetTarget3DPosition() const { return target3DPos_; }
  const Vector3& GetRayDirection() const { return rayDirection_; }

  Config& GetConfig() { return config_; }

private:
  Config config_;
  SpriteRenderer* spriteRenderer_ = nullptr;

  float lockOnLerp_ = 0.0f; // ロックオン状態のアニメーション用（0.0〜1.0）

  Vector2 position_ = { 640.0f, 360.0f }; // 2Dスクリーン座標（中央 1280x720 想定）

  Vector3 target3DPos_ = { 0.0f, 0.0f, 0.0f };  // 3D空間上の射撃目標地点
  Vector3 rayDirection_ = { 0.0f, 0.0f, 1.0f }; // 3D視線の単位方向ベクトル

  float spinRot_ = 0.0f; // トンネル全体（3つの枠共通）の累積回転角

  static constexpr float kSpinReturnRate_ = 0.2f;  // ロックオン解除後、枠が正位置へ戻る速さ（毎フレームの補間率）
  static constexpr float kQuarterTurn_ = std::numbers::pi_v<float> * 0.5f; // 正方形は90度ごとに同じ形になる

  static constexpr float kBracketRatio_ = 0.3f;        // 手前枠の角かぎの長さ（辺の長さに対する割合）
  static constexpr float kConnectorNearAlpha_ = 0.7f;  // 手前→中の接続線の不透明度
  static constexpr float kConnectorFarAlpha_ = 0.45f;  // 中→奥の接続線の不透明度
  static constexpr float kConnectorThickness_ = 2.0f;  // 接続線の太さ（ピクセル）
  static constexpr float kCenterDotSize_ = 4.0f;       // 奥枠の中心点の大きさ（ピクセル）

  // トンネル構造を描画するための直線スプライト群（アウトライン+本体の2重で描く線は2本ずつ）
  // 接続線：手前→中 4本 + 中→奥 4本 = 8本（アウトラインなし）
  // 手前の角かぎ：4角 × 2辺 × 2重 = 16本
  // 中・奥の四角：4辺 × 2重 × 2枠 = 16本
  // 奥の中心点：2重 = 2本
  static constexpr size_t kMaxLineSprites = 42;
  std::array<std::unique_ptr<Sprite>, kMaxLineSprites> lineSprites_;

  // スプライトの更新ヘルパー
  void UpdateLineSprite(size_t index, const Vector2& p1, const Vector2& p2, float thickness, const Vector4& color);
};
