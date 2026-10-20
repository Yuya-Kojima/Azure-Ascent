#pragma once
#include "Audio/SoundManager.h"
#include "Core/EngineBase.h"
#include "Math/MathUtil.h"
#include "Scene/BaseScene.h"
#include <string>
#include <unordered_map>
#include <vector>

class Sprite;
class Object3d;
class SpriteRenderer;
class Object3dRenderer;
class DebugCamera;
class InputKeyState;
class ParticleEmitter;
class PostProcess;
class BillboardParticleEmitter;
class MeshParticleEmitter;
class StageIntro;

#include "Actor/Boss.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include "Render/SkyBox/SkyBox.h"

struct SpawnEvent {
  float spawnTime = 0.0f;
  std::string prefabName = "ZakoEnemy";
  Vector3 spawnOffset = {0.0f, 0.0f, 50.0f}; // カメラからの相対位置、またはワールド絶対座標
  bool isWorldSpace = false;        // trueならspawnOffsetをワールド絶対座標として扱う
  std::string splineName = "";     // 使用するレール名（空なら直線移動）
  float splineDuration = 5.0f;     // レールを走り切る秒数
  bool isWorldSpaceSpline = false; // ワールド空間か、カメラローカル空間か
  int fireInterval = -1;           // -1なら撃たない。0以上なら指定フレーム間隔で射撃
  bool hasSpawned = false;         // 実行管理用フラグ
  MoveType moveType = MoveType::Straight;
  Vector3 moveDirection = {0.0f, 0.0f, 1.0f}; // カメラ基準の移動方向ベクトル（デフォルトは前進）
};

enum class GameState { Intro, Play, Clear, GameOver };

class GamePlayScene : public BaseScene {

private: // メンバ変数(ゲーム用)
  // カメラ
  std::unique_ptr<GameCamera> camera_ = nullptr;
  Transform cameraTransform_{};

  // レールカメラ
  std::unique_ptr<RailCamera> railCamera_ = nullptr;
  std::vector<Vector3> waypoints_;

  // スカイボックス
  std::unique_ptr<Skybox> skybox_ = nullptr;

  // プレイヤー
  std::unique_ptr<Player> player_ = nullptr;

  // デバッグカメラ
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // デバッグカメラスイッチ
  bool useDebugCamera_ = false;

  // プレイ/ストップモードフラグ
  bool previousGlobalPlayMode_ = false;
  bool isPaused_ = false;
  bool doStep_ = false;
  float playStartT_ = 0.0f; // Play開始時のtの値を保存しておく変数

  // ゲーム状態
  GameState gameState_ = GameState::Play;

  // ステージ開始演出
  std::unique_ptr<StageIntro> stageIntro_;
  // Play 開始時に開始演出を再生するか
  bool playIntroOnStart_ = true;
  // 雲海の面の高さの既定値（配置オブジェクトに雲海がないときに使う）
  static constexpr float kDefaultSeaHeight_ = -15.0f;
  // 開始の合図（操作開始の瞬間に鳴らすドラゴンの吠え声）の音量
  static constexpr float kRoarSeVolume_ = 1.0f;
  // 羽ばたきを進める1フレームの時間（秒）
  static constexpr float kWingDeltaTime_ = 1.0f / 60.0f;

  // 開始演出のあと、HUD と照準が現れるまでの時間（秒）
  float hudFadeSec_ = 0.3f;
  float hudFadeTimer_ = 0.3f;

  // 乗っ取られた空（手続き生成の空・ライト・霧）
  // 画面の色は「線形の値 × 露出 0.5 → ACES トーンマップ → sRGB」で決まる
  float skyTimer_ = 0.0f;                             // 雲を流すための経過時間（秒）
  Vector3 skyZenith_ = {0.069f, 0.047f, 0.104f};      // 天頂の色
  Vector3 skyHorizon_ = {0.208f, 0.088f, 0.104f};     // 地平線の色
  Vector3 skySunDir_ = {0.35f, 0.10f, 1.0f};          // 太陽の方向（進行方向の右寄り、地平線の近く）
  Vector3 skySunColor_ = {1.0f, 0.30f, 0.16f};        // 太陽の色
  float skySunIntensity_ = 1.4f;                      // 太陽の強さ
  float skyCloudAmount_ = 0.75f;                      // 雲の量（0〜1）
  Vector3 skyCloudColor_ = {0.031f, 0.028f, 0.043f};  // 雲の色
  float skyCloudOpacity_ = 0.92f;                     // 雲の不透明度
  float skyCloudRim_ = 0.2f;                          // 太陽側で雲の縁が光る強さ
  Vector3 lightColor_ = {0.85f, 0.80f, 0.95f};        // 平行光源の色
  float lightIntensity_ = 0.85f;                      // 平行光源の強さ

  // ヒットストップタイマー（被弾・ボス演出用）
  int hitStopTimer_ = 0;
  int hitStopBossPartFrames_ = 6;      // ボス部位破壊・死亡演出時のヒットストップ（デフォルト6フレーム）
  int hitStopCooldown_ = 4;           // 連続着弾による多重停止を防ぐクールダウン
  int hitStopCooldownTimer_ = 0;

  // ダミー敵管理
  std::vector<std::unique_ptr<Enemy>> runtimeEnemies_;
  std::vector<Enemy *> enemyPtrs_;

  // ロード済みスプラインデータ (Blender JSON等から)
  std::unordered_map<std::string, std::vector<Vector3>> loadedSplines_;
  void LoadSplines(); // splines.jsonを読み込んでloadedSplines_に格納

  // スポーンイベント
  std::vector<SpawnEvent> spawnEvents_;

  // 動的配置オブジェクト (ドラッグ＆ドロップで追加された背景・モデル等)
  std::vector<std::unique_ptr<Object3d>> sceneObjects_;

  // ドラッグ中のプレビュー用オブジェクト
  std::unique_ptr<Object3d> previewObject_ = nullptr;
  std::string previewModelPath_ = "";
  bool isPreviewHovering_ = false;


  // ボス専用エミッター
  std::unique_ptr<BillboardParticleEmitter> bossExplosionParticleGroup_;
  std::unique_ptr<BillboardParticleEmitter> bossDustParticleGroup_;
  std::unique_ptr<ParticleEmitter> bossExplosionEmitter_;
  std::unique_ptr<ParticleEmitter> bossDustEmitter_;

  bool hasBossStartedDying_ = false;

  // ポストエフェクト
  bool isGrayscale_ = false;

  // エディタ用：選択中のオブジェクトタイプ
  enum class EditorSelectType {
    None,
    RailCamera,
    Environment,
    SceneObject,
    SpawnEvent,
    Player,
    Effect,
    Prefab
  };
  EditorSelectType currentSelectType_ = EditorSelectType::None;
  int selectedWaypointIndex_ = -1;
  int selectedSceneObjectIndex_ = -1;
  int selectedSpawnEventIndex_ = -1;

  // プレハブ編集用
  std::string selectedPrefabName_ = "";
  std::unique_ptr<Enemy> tempPrefabEditEnemy_ = nullptr;

  // HPバー用
  float hpBarBaseWidth_ = 0.0f;

public: // Undo/Redo用アクセッサ
  std::vector<SpawnEvent> &GetSpawnEvents() { return spawnEvents_; }
  void SelectSpawnEvent(int index) {
    selectedSpawnEventIndex_ = index;
    currentSelectType_ =
        (index >= 0) ? EditorSelectType::SpawnEvent : EditorSelectType::None;
  }

public: // メンバ関数
  GamePlayScene();
  ~GamePlayScene() override;

  /// <summary>
  /// ヒットストップの要求（フレーム数指定、複数回呼ばれた場合は長い方を優先）
  /// </summary>
  void RequestHitStop(int frames);

  /// <summary>
  /// ステージで使う重い準備（敵のモデルを読んで割っておく、効果音・BGM を読む）
  /// 済んでいればすぐ戻るので、起動時とシーンの初期化の両方で呼ぶ
  /// </summary>
  static void PreloadResources(EngineBase *engine);

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
  /// エディタ用UIとGizmoの描画
  /// </summary>
  void DrawEditorUI() override;
  void OnFileDropped(const std::string &filePath,
                     const Vector2 &ndcPos) override;
  void OnDragHovering(const std::string &filePath,
                      const Vector2 &ndcPos) override;
  void OnDragHoverEnd() override;

  // Waypoint
  void AppendWaypoint(const Vector3 &pos);

private:
  /// <summary>
  /// ステージ開始演出を始める（レールを止め、操作開始時の自機とカメラの位置を決めてから渡す）
  /// </summary>
  void StartIntro();

  /// <summary>
  /// ステージ開始演出の更新（レールの加速・吠え声の合図・姿勢を渡し、飛び出しに合わせて操作を開始する）
  /// </summary>
  void UpdateIntro();

  /// <summary>
  /// 開始演出から操作へ移る間、演出が決める自機の位置・向きと操作の位置・向きを混ぜる
  /// </summary>
  void BlendPlayerWithIntro();

  /// <summary>
  /// 空・ライト・霧を設定する
  /// </summary>
  void ApplySkyAndLighting();

  void SpawnSceneObject(const std::string &modelPath, const Vector3 &position);

  /// <summary>
  /// レベルデータの読み込み
  /// </summary>
  void LoadLevel(const std::string &filename = "level_editor.json");

  /// <summary>
  /// レベルデータの保存
  /// </summary>
  void SaveLevel(const std::string &filename = "level_editor.json");

private: // メンバ変数(システム用)
private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
