#pragma once
#include "Audio/SoundManager.h"
#include "Core/EngineBase.h"
#include "Math/MathUtil.h"
#include "Scene/BaseScene.h"
#include <vector>

class Sprite;
class Object3d;
class SpriteRenderer;
class Object3dRenderer;
class DebugCamera;
class InputKeyState;
class Skybox;
class ICamera;

class StageSelectScene : public BaseScene {

public:  // メンバ関数
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
  std::unique_ptr<Object3d> dragonObject_ = nullptr;
  Vector3 dragonPos_ = {2.0f, 0.5f, 2.0f};
  Vector3 dragonRot_ = {-0.25f, -0.5f, 0.0f};
  float dragonScale_ = 1.7f;

  // 地上からやや見上げるカメラ
  Vector3 cameraPos_ = {0.0f, 0.8f, -6.0f};
  Vector3 cameraRot_ = {-0.12f, 0.0f, 0.0f};

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
