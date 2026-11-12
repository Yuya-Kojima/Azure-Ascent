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
class Input;
class ParticleEmitter;
class PostProcess;
class BillboardParticleEmitter;
class MeshParticleEmitter;
class StageIntro;
class GameOverSequence;
class SkipHold;
class StageLighting;
class RoarDistortion;
class CloudSea;
class CloudSeaEvents;
class GameHud;
class ResultSequence;
class PauseMenu;
class ScoreKeeper;
class FloatingFortress;
class InterceptCue;
class SerpentCameraDirector;

#include "Actor/Boss.h"
#include "Actor/Enemy.h"
#include "Actor/MechSerpent.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include "Render/SkyBox/SkyBox.h"
#include "Score/ScoreConfig.h"

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
  SerpentSpec serpent;             // 大蛇（MechSerpent）の出番の決まり。ほかの出し方では使わない
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

  // 評価: 点の入り方とランクの基準、1 回のプレイの点と撃墜率、HUD、クリアしたあとのリザルト
  ScoreConfig scoreConfig_;
  std::unique_ptr<ScoreKeeper> scoreKeeper_;
  std::unique_ptr<GameHud> hud_;
  std::unique_ptr<ResultSequence> resultSequence_;

  // ポーズ画面（プレイ中に開き、開いている間は世界の時間を止める）
  std::unique_ptr<PauseMenu> pauseMenu_;
  static constexpr float kPauseBgmVolume_ = 0.35f; // ポーズ中の BGM の音量（0〜1）
  // リトライ・拠点へ戻るときの白い雲の遷移
  static constexpr float kCloudTransitionSec_ = 1.0f; // 秒
  static constexpr Vector4 kCloudTransitionColor_ = {1.0f, 1.0f, 1.0f, 1.0f};

  // ステージ開始演出
  std::unique_ptr<StageIntro> stageIntro_;
  // Play 開始時に開始演出を再生するか
  bool playIntroOnStart_ = true;
  // 開始の合図（操作開始の瞬間に鳴らすドラゴンの吠え声）の音量
  static constexpr float kRoarSeVolume_ = 1.0f;
  // 羽ばたきを進める1フレームの時間（秒）
  static constexpr float kWingDeltaTime_ = 1.0f / 60.0f;

  // ゲームオーバーの空中の演出
  std::unique_ptr<GameOverSequence> gameOverSequence_;
  // 開始演出とゲームオーバー演出を飛ばす長押し
  std::unique_ptr<SkipHold> skipHold_;
  // ゲームオーバーの間、世界の時間の速さを足していき、1 を超えたフレームだけ世界を進める
  float worldTimeCarry_ = 0.0f;

  // 開始演出のあと、HUD と照準が現れるまでの時間（秒）
  float hudFadeSec_ = 0.3f;
  float hudFadeTimer_ = 0.3f;

  // 空・光・霧・ポストエフェクトの値
  std::unique_ptr<StageLighting> stageLighting_;
  float skyTimer_ = 0.0f; // 雲を流すための経過時間（秒）

  // 咆哮の空間のゆがみ
  std::unique_ptr<RoarDistortion> roarDistortion_;

  // 雲海と、雲海を動かす出来事（盛り上がり・裂け目）
  std::unique_ptr<CloudSea> cloudSea_;
  std::unique_ptr<CloudSeaEvents> cloudSeaEvents_;

  // ヒットストップタイマー（被弾・ボス演出用）
  int hitStopTimer_ = 0;
  int hitStopBossPartFrames_ = 6;      // ボス部位破壊・死亡演出時のヒットストップ（デフォルト6フレーム）
  int hitStopCooldown_ = 4;           // 連続着弾による多重停止を防ぐクールダウン
  int hitStopCooldownTimer_ = 0;

  // ダミー敵管理
  std::vector<std::unique_ptr<Enemy>> runtimeEnemies_;
  // 浮かぶ砦（部品は runtimeEnemies_ に入る）
  std::vector<std::unique_ptr<FloatingFortress>> fortresses_;
  static inline const std::string kFortressPrefabName_ = "FloatingFortress"; // 砦を出す出し方の名前
  static constexpr Vector4 kFortressPartColor_ = {1.0f, 0.2f, 0.2f, 1.0f};      // 砦の部品の色（ふだんの敵と同じ赤）
  // 区間 3 の大蛇の出番（部品は runtimeEnemies_ に入る）
  std::vector<std::unique_ptr<MechSerpent>> serpents_;
  static inline const std::string kSerpentPrefabName_ = "MechSerpent"; // 大蛇を出す出し方の名前
  // 迎撃の印（大蛇の光る節のまわりの放電と、自機へ走る電撃）
  std::unique_ptr<InterceptCue> interceptCue_;
  // 大蛇の出来事でカメラとレールを動かす
  std::unique_ptr<SerpentCameraDirector> serpentCamera_;
  std::vector<Enemy *> enemyPtrs_;

  // ロード済みスプラインデータ (Blender JSON等から)
  std::unordered_map<std::string, std::vector<Vector3>> loadedSplines_;
  void LoadSplines(); // splines.jsonを読み込んでloadedSplines_に格納

  // スポーンイベント
  std::vector<SpawnEvent> spawnEvents_;

  // 動的配置オブジェクト (ドラッグ＆ドロップで追加された背景・モデル等)
  std::vector<std::unique_ptr<Object3d>> sceneObjects_;
  // 配置オブジェクトのうち、敵を隠す箱（ワールド → 箱の中の座標の行列。毎フレーム作り直す）
  std::vector<Matrix4x4> lockOnOccluders_;
  static inline const std::string kOccluderModelPath_ = "__builtin_box"; // 敵を隠す箱のモデル（エンジン組み込みの一辺 1 の箱）
  static inline const std::string kTowerPanelModelPath_ = "resources/models/tower/tower_panel.obj"; // 敵を隠す区間 3 の塔の面（一辺 1 の箱に格子を貼ったモデル）

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
  /// ゲームオーバーの空中の演出を始める（レール・BGM を止める）
  /// </summary>
  void StartGameOver();

  /// <summary>
  /// ゲームオーバーの空中の演出の更新（自機の位置・向き・翼を渡し、合図で拠点への遷移を始める）
  /// </summary>
  void UpdateGameOver();

  /// <summary>
  /// クリアしてリザルトを出し始める（点を確定し、記録を書く）
  /// </summary>
  void StartClear();

  /// <summary>
  /// リザルトの更新（選んだら次のシーンへ）
  /// </summary>
  void UpdateClear();

  /// <summary>
  /// ポーズ画面の更新（プレイ中に開き、選んだら再開・次のシーンへ）
  /// </summary>
  /// <param name="input">操作を受けるときの入力。nullptr なら操作を受けない</param>
  /// <returns>このフレームの世界を止めるか（開いたフレームと閉じたフレームも止める）</returns>
  bool UpdatePauseMenu(Input *input);

  /// <summary>
  /// 拠点へ降りずに、雲を抜けてもう一度ステージの開始演出から始める
  /// </summary>
  void RetryStage();

  /// <summary>
  /// 雲を抜けて降下し、ステージセレクト（雲の下の拠点）へ戻る
  /// </summary>
  void ReturnToBase();

  /// <summary>
  /// ボスが出るレールの進み具合（ボスがいなければレールの終わり）
  /// </summary>
  float GetBossSpawnTime() const;

  /// <summary>
  /// 出ているボス（いなければ nullptr）
  /// </summary>
  const Boss *FindBoss() const;

  /// <summary>
  /// 空・雲海・光・霧・ポストエフェクトを設定する
  /// </summary>
  void ApplySkyAndLighting();

  void SpawnSceneObject(const std::string &modelPath, const Vector3 &position);

  /// <summary>
  /// 浮かぶ砦と部品 8 個を出す。部品は撃破に数える敵として登録する
  /// </summary>
  void SpawnFortress(const SpawnEvent &ev);

  /// <summary>
  /// 大蛇の 1 回の出番と、その光る節を出す。光る節は撃破に数える敵として登録する
  /// </summary>
  void SpawnSerpent(const SpawnEvent &ev);

  /// <summary>
  /// 出ている敵と砦と大蛇をすべて消す
  /// </summary>
  void ClearRuntimeEnemies();

  /// <summary>
  /// レベルデータの読み込み
  /// </summary>
  void LoadLevel(const std::string &filename = "level_editor.json");

  /// <summary>
  /// レベルデータの保存
  /// </summary>
  void SaveLevel(const std::string &filename = "level_editor.json");

  /// <summary>
  /// カメラレール（waypoints と顔の向きのキー）を camera_rail.json に保存
  /// </summary>
  void SaveCameraRail();

  /// <summary>
  /// インスペクターの「カメラの向き」欄（顔の向きのキーの一覧・追加・削除・保存）
  /// </summary>
  void DrawLookKeysUI();

private: // メンバ変数(システム用)
private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
