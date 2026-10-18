#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Audio/SoundManager.h"
#include "Core/EngineBase.h"
#include "Math/MathUtil.h"
#include "Scene/BaseScene.h"
#include <vector>

class Sprite;
class Object3d;
class DragonModel;
class SpriteRenderer;
class Object3dRenderer;
class DebugCamera;
class InputKeyState;
class Skybox;
class ICamera;

class StageSelectScene : public BaseScene {

public:  // メンバ関数
  StageSelectScene();
  ~StageSelectScene() override;

  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(EngineBase *engine) override;

  /// <summary>
  /// 終了
  /// </summary>
  void Finalize() override;

  /// <summary>
  /// 更新
  /// </summary>
  void Update() override;

  /// <summary>
  /// 描画
  /// </summary>
  void Draw() override;

  /// <summary>
  /// 2Dオブジェクト描画
  /// </summary>
  void Draw2D() override;

  /// <summary>
  /// 3Dオブジェクト描画
  /// </summary>
  void Draw3D() override;

  /// <summary>
  /// エディタ用UI描画（拠点の配置調整）
  /// </summary>
  void DrawEditorUI() override;

private: // メンバ関数
  /// <summary>
  /// 空・ライト・フォグを設定する
  /// </summary>
  void UpdateSky(const ICamera *activeCamera);

  /// <summary>
  /// 拠点のオブジェクト（地面・ドラゴン）に配置を反映する
  /// </summary>
  void ApplyBaseLayout();

  /// <summary>
  /// 出撃演出を始める
  /// </summary>
  void StartLaunch();

  /// <summary>
  /// 出撃演出の更新（S1 かがむ → S2 飛び立ち → S3 雲のフェード）
  /// </summary>
  void UpdateLaunch();

private: // メンバ変数(システム用)
  // カメラ
  std::unique_ptr<GameCamera> camera_ = nullptr;

  // デバッグカメラ
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // デバッグカメラ使用
  bool useDebugCamera_ = false;

private: // メンバ変数(拠点)
  // 空（手続き生成の空。頭上の雲もここで描く）
  std::unique_ptr<Skybox> skybox_ = nullptr;
  // 雲を流すための経過時間（秒）
  float skyTimer_ = 0.0f;

  // 地面
  std::unique_ptr<Object3d> groundObject_ = nullptr;
  Vector3 groundPos_ = {0.0f, -3.0f, 60.0f};
  Vector3 groundScale_ = {30.0f, 4.0f, 30.0f};

  // 拠点で待機するドラゴン（メニューの文字と重ならないよう画面の右寄りに置く）
  std::unique_ptr<DragonModel> dragonModel_ = nullptr;
  Object3d *dragonObject_ = nullptr; // dragonModel_ の Object3d（位置・向き・大きさの設定用）
  Vector3 dragonPos_ = {2.0f, 0.5f, 2.0f};
  Vector3 dragonRot_ = {-0.25f, -0.5f, 0.0f};
  float dragonScale_ = 1.7f;

  // 羽ばたき（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）。地上では立ち姿勢、飛び立つと飛行姿勢
  DragonWingAnimator::State idleWing_ = {0.1f, 0.5f, 0.4f, 0.0f, 0.0f};   // 待機
  DragonWingAnimator::State crouchWing_ = {0.0f, 0.5f, 0.8f, 0.0f, 0.0f}; // かがむ
  DragonWingAnimator::State leapWing_ = {1.0f, 2.6f, 0.0f, 0.0f, 1.0f, 1.0f}; // 飛び立ち
  static constexpr float kWingDeltaTime_ = 1.0f / 60.0f; // 羽ばたきを進める1フレームの時間（秒）

  // 地上からやや見上げるカメラ
  Vector3 cameraPos_ = {0.0f, 0.8f, -6.0f};
  Vector3 cameraRot_ = {-0.12f, 0.0f, 0.0f};

private: // メンバ変数(出撃演出)
  bool isLaunching_ = false;
  // 出撃を押してからの経過時間（秒）
  float launchTimer_ = 0.0f;
  // 雲のフェードでシーン切り替えを要求したか
  bool hasRequestedSceneChange_ = false;

  // S1 かがむ
  float menuFadeSec_ = 0.2f;     // メニューが消えるまでの時間（秒）
  float crouchSec_ = 0.45f;      // 沈み込む時間（秒）
  float crouchHoldSec_ = 0.15f;  // かがんだまま止まる時間（秒）
  float crouchDepth_ = 0.3f;     // 沈む深さ
  float crouchPitch_ = 0.25f;    // かがんだときに機首を下げる角度（ラジアン）
  float dollyDistance_ = 0.5f;   // かがむ間にカメラがドラゴンへ寄る距離

  // S2 飛び立ち
  float flightSec_ = 1.2f;        // 飛び立ってから雲のフェードを始めるまでの時間（秒）
  float leapSpeed_ = 20.0f;       // 跳ねた瞬間の上昇速度（単位/秒）
  float cruiseSpeed_ = 8.0f;      // 跳ねた勢いが落ちたあとの上昇速度（単位/秒）
  float leapDecaySec_ = 0.12f;    // 跳ねた勢いが落ちる速さ（指数関数の時定数、秒）
  float forwardRatio_ = 0.15f;    // 上昇量に対する、向いている方向への前進量の割合
  float flightPitch_ = -0.9f;     // 上昇中の機首の角度（ラジアン。負で上を向く）
  float pitchUpSec_ = 0.25f;      // 機首を上げきるまでの時間（秒）
  float craneFollow_ = 0.7f;      // ドラゴンの移動量のうち、カメラがついて行く割合
  float cameraTurnSec_ = 0.25f;   // カメラがドラゴンへ向ききるまでの時間（秒）
  float lookBelowDragon_ = 0.6f;  // カメラの注視点をドラゴンより下げる量
  float leapShake_ = 0.08f;       // 跳ねた瞬間のカメラの揺れの大きさ
  float leapShakeSec_ = 0.25f;    // 揺れが収まるまでの時間（秒）

  // S3 雲のフェード
  float cloudFadeSec_ = 0.6f;     // 雲が覆う時間と晴れる時間（秒）

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
