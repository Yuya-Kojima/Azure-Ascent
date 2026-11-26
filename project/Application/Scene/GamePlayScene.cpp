#include "GamePlayScene.h"
#include "../../externals/nlohmann/json.hpp"
#include "../Effect/EffectManager.h"
#include "Actor/DragonModel.h"
#include "Actor/EnemyPart.h"
#include "Actor/FloatingFortress.h"
#include "Actor/ILockOnHittable.h"
#include "Actor/InterceptCue.h"
#include "Actor/MechSerpent.h"
#include "Audio/SoundManager.h"
#include "Camera/GameCamera.h"
#include "Camera/SerpentCameraDirector.h"
#include "Camera/BossCameraDirector.h"
#include "Debug/DebugCamera.h"
#include "Debug/ImGuiManager.h"
#include "Debug/Logger.h"
#include "Effect/FractureCache.h"
#include "Effect/FractureDebris.h"
#include "Effect/RoarDistortion.h"
#include "Framework/MenuInput.h"
#include "Framework/UIManager.h"
#include "Input/InputKeyState.h"
#include "Math/MathUtil.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Object3d/Object3d.h"
#include "Particle/Particle.h"
#include "Particle/ParticleEmitter.h"
#include "Particle/ParticleManager.h"
#include "Render/Particle/BillboardParticleEmitter.h"
#include "Render/Particle/MeshParticleEmitter.h"
#include "Render/Renderer/LineRenderer.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/SpriteRenderer.h"
#include "Renderer/TrailRenderer.h"
#include "Scene/SceneManager.h"
#include "Scene/DarkCloudTransition.h"
#include "Sprite/Sprite.h"
#include "StageIntro.h"
#include "GameOverSequence.h"
#include "ClearSequence.h"
#include "GameHud.h"
#include "PauseMenu.h"
#include "ResultSequence.h"
#include "Score/ScoreKeeper.h"
#include "Score/StageRecord.h"
#include "Scene/State/IntroState.h"
#include "Scene/State/PlayState.h"
#include "Spawn/BossSpawner.h"
#include "Spawn/FortressSpawner.h"
#include "Spawn/SerpentSpawner.h"
#include "Spawn/ZakoSpawner.h"
#include "SkipHold.h"
#include "Texture/TextureManager.h"
#include <filesystem>
#include <fstream>

#include "../Editor/CommandManager.h"
#include "Collision/CollisionManager.h"
#include "Framework/ActorManager.h"
#include "Framework/GameManager.h"
#include "Framework/PrefabManager.h"
#include "Renderer/PostProcess.h"
#include "Render/CloudSea/CloudSea.h"
#include "Scene/CloudSeaEvents.h"
#include "Scene/StageLighting.h"

// ======================================
// Undo/Redo用コマンドクラス
// ======================================
class CmdAddSpawnEvent : public ICommand {
  GamePlayScene *scene_;
  SpawnEvent event_;
  int index_;

public:
  CmdAddSpawnEvent(GamePlayScene *scene, const SpawnEvent &ev, int idx)
      : scene_(scene), event_(ev), index_(idx) {}
  void Execute() override {
    auto &events = scene_->GetSpawnEvents();
    events.insert(events.begin() + index_, event_);
    scene_->SelectSpawnEvent(index_);
  }
  void Undo() override {
    auto &events = scene_->GetSpawnEvents();
    events.erase(events.begin() + index_);
    scene_->SelectSpawnEvent(-1);
  }
};

class CmdDeleteSpawnEvent : public ICommand {
  GamePlayScene *scene_;
  SpawnEvent event_;
  int index_;

public:
  CmdDeleteSpawnEvent(GamePlayScene *scene, int idx)
      : scene_(scene), index_(idx) {
    event_ = scene_->GetSpawnEvents()[idx];
  }
  void Execute() override {
    auto &events = scene_->GetSpawnEvents();
    events.erase(events.begin() + index_);
    scene_->SelectSpawnEvent(-1);
  }
  void Undo() override {
    auto &events = scene_->GetSpawnEvents();
    events.insert(events.begin() + index_, event_);
    scene_->SelectSpawnEvent(index_);
  }
};

class CmdModifySpawnEvent : public ICommand {
  GamePlayScene *scene_;
  int index_;
  SpawnEvent oldEvent_;
  SpawnEvent newEvent_;

public:
  CmdModifySpawnEvent(GamePlayScene *scene, int idx, const SpawnEvent &oldEv,
                      const SpawnEvent &newEv)
      : scene_(scene), index_(idx), oldEvent_(oldEv), newEvent_(newEv) {}
  void Execute() override {
    scene_->GetSpawnEvents()[index_] = newEvent_;
    scene_->SelectSpawnEvent(index_);
  }
  void Undo() override {
    scene_->GetSpawnEvents()[index_] = oldEvent_;
    scene_->SelectSpawnEvent(index_);
  }
};

// ======================================
#ifdef USE_IMGUI
#include "ImGuizmo.h"
#include <imgui.h>
#endif
#include <iomanip>
#include <numbers>
#include <string>

GamePlayScene::GamePlayScene() = default;

GamePlayScene::~GamePlayScene() = default;

void GamePlayScene::RequestHitStop(int frames) {
  if (frames <= 0) {
    return;
  }
  // クールダウン中かつ既にストップ中なら多重ストップを防止してテンポを守る
  if (hitStopCooldownTimer_ > 0 && hitStopTimer_ > 0) {
    return;
  }
  hitStopTimer_ = (std::max)(hitStopTimer_, frames);
  hitStopCooldownTimer_ = frames + hitStopCooldown_;
}

void GamePlayScene::PreloadResources(EngineBase *engine) {
  PrefabManager::GetInstance()->Initialize(engine->GetObject3dRenderer());

  // ロード時に事前にモデルを割る
  // 撃破時に割ると、その瞬間に計算とGPUバッファの作成が集中してカクつくため
  for (const char *prefabName :
       {"ZakoEnemy", "FastEnemy", "TankEnemy", "ZakoEye", "ZakoArrow", "Boss"}) {
    // 光る部品のモデルも読み込んでおく（光る部品は割らない）
    PrefabManager::GetInstance()->PreloadLightModels(prefabName);
    // プレハブからモデルのパスを読む
    std::string modelPath =
        PrefabManager::GetInstance()->GetModelPath(prefabName);
    // そのモデルを読み込む（読み込み済みなら何もしない）
    ModelManager::GetInstance()->LoadModel(modelPath);
    // 読み込んだモデルを探して、割っておく（割ってあれば何もしない）
    FractureCache::GetInstance()->Prebuild(
        ModelManager::GetInstance()->FindModel(modelPath));
  }

  // 効果音・BGM（読み込み済みなら何もしない）
  SoundManager::GetInstance()->Load("boss_explosion",
                                    "resources/Sounds/explosion.mp3");
  SoundManager::GetInstance()->Load("laser_shot",
                                    "resources/Sounds/laser_shot.wav");
  SoundManager::GetInstance()->Load("lockon", "resources/Sounds/lockon.wav");
  SoundManager::GetInstance()->Load("lockon_fire",
                                    "resources/Sounds/lockon_fire.wav");
  SoundManager::GetInstance()->Load("enemy_hit",
                                    "resources/Sounds/enemy_hit.wav");
  SoundManager::GetInstance()->Load("enemy_destroy",
                                    "resources/Sounds/enemy_destroy.wav");
  SoundManager::GetInstance()->Load("player_damage",
                                    "resources/Sounds/player_damage.wav");
  SoundManager::GetInstance()->Load("boss_charge",
                                    "resources/Sounds/boss_charge.wav");
  SoundManager::GetInstance()->Load("intercept_warn",
                                    "resources/Sounds/intercept_warn.wav");
  SoundManager::GetInstance()->Load("intercept_lock",
                                    "resources/Sounds/intercept_lock.wav");
  SoundManager::GetInstance()->Load("boss_counter",
                                    "resources/Sounds/boss_counter.mp3");
  SoundManager::GetInstance()->Load("ui_decide",
                                    "resources/Sounds/ui_decide.wav");
  SoundManager::GetInstance()->Load("stage_bgm",
                                    "resources/Sounds/stage_bgm.mp3");
  SoundManager::GetInstance()->Load("boss_bgm",
                                    "resources/Sounds/boss_bgm.mp3");
  SoundManager::GetInstance()->Load("victory_jingle",
                                    "resources/Sounds/victory_jingle.mp3");
  SoundManager::GetInstance()->Load("dragon_roar",
                                    "resources/Sounds/dragon-cry2.mp3");
}

void GamePlayScene::Initialize(EngineBase *engine) {
  // 基底クラスの初期化 (PostProcessの初期化など)
  BaseScene::Initialize(engine);

  state_ = std::make_unique<PlayState>(*this);

  // シーン初期化時に前シーンの残留アクター（弾や古いプレイヤー）を全消去
  ActorManager::GetInstance()->Clear();
  CollisionManager::GetInstance()
      ->Clear(); // コライダーの残留（ダングリングポインタ）を防ぐ
  // ステージで使う重い準備（起動時に済んでいれば、すぐ戻る）
  PreloadResources(engine);

  // 参照をコピー
  engine_ = engine;

  //===========================
  // テクスチャファイルの読み込み
  //===========================

  //===========================
  // BGM の再生（読み込みは PreloadResources）
  //===========================
  SoundManager::GetInstance()->PlayBGM("stage_bgm");

  EffectManager::GetInstance()->Initialize();

  //===========================
  // スプライト関係の初期化
  //===========================

  //===========================
  // 3Dオブジェクト関係の初期化
  //===========================

  // デバッグカメラの初期化（開発用の自由カメラ）真っ直ぐ奥へ進むだけの自然なレールに変更）
  waypoints_ = {{0.0f, 4.0f, -10.0f}, {0.0f, 4.0f, 40.0f},
                {0.0f, 4.0f, 90.0f},  {0.0f, 4.0f, 140.0f},
                {0.0f, 4.0f, 190.0f}, {0.0f, 4.0f, 215.0f},
                {0.0f, 4.0f, 230.0f}};
  railCamera_ = std::make_unique<RailCamera>();
  railCamera_->Initialize(waypoints_);
  railCamera_->SetSpeed(0.2f); // スピードも少し落として照準を合わせやすくする

#ifdef USE_IMGUI
  // エディタモードの初期状態：デバッグカメラON、自動進行OFF
  useDebugCamera_ = true;
  railCamera_->SetAutoMove(false);
#else
  // Release版の初期状態：デバッグカメラOFF、自動進行ON
  useDebugCamera_ = false;
  railCamera_->SetAutoMove(true);
#endif

  // デフォルトカメラをレールカメラに設定
  engine_->GetObject3dRenderer()->SetDefaultCamera(railCamera_.get());

  //===========================
  // SkyBoxの初期化
  //===========================
  TextureManager::GetInstance()->LoadTexture("resources/Skybox/Skybox.dds");
  skybox_ = std::make_unique<Skybox>();
  skybox_->Initialize(engine_->GetSkyboxRenderer());
  skybox_->SetTexture("resources/Skybox/Skybox.dds");
  skybox_->SetScale({100.0f, 100.0f, 100.0f});
  skybox_->SetProceduralSky(true);
  stageLighting_ = std::make_unique<StageLighting>();
  stageLighting_->Initialize();
  roarDistortion_ = std::make_unique<RoarDistortion>();
  roarDistortion_->Initialize();

  // 雲海と、雲海を動かす出来事
  cloudSea_ = std::make_unique<CloudSea>();
  cloudSea_->Initialize(engine_->GetCloudSeaRenderer());
  cloudSeaEvents_ = std::make_unique<CloudSeaEvents>();
  cloudSeaEvents_->Initialize();

  // 空・雲海・光・霧・ポストエフェクト
  ApplySkyAndLighting();


  // ステージ開始演出
  stageIntro_ = std::make_unique<StageIntro>();
  stageIntro_->Initialize(engine_->GetSpriteRenderer(), cloudSeaEvents_.get());
  gameOverSequence_ = std::make_unique<GameOverSequence>();
  gameOverSequence_->Initialize(engine_->GetSpriteRenderer());
  clearSequence_ = std::make_unique<ClearSequence>();
  clearSequence_->Initialize(engine_->GetSpriteRenderer(),
                             engine_->GetObject3dRenderer());

  // 迎撃の印と、大蛇の出来事で動かすカメラ
  interceptCue_ = std::make_unique<InterceptCue>();
  interceptCue_->Initialize(engine_->GetSpriteRenderer());
  zakoSpawner_ = std::make_unique<ZakoSpawner>();
  bossSpawner_ = std::make_unique<BossSpawner>();
  fortressSpawner_ = std::make_unique<FortressSpawner>();
  serpentSpawner_ = std::make_unique<SerpentSpawner>();
  serpentCamera_ = std::make_unique<SerpentCameraDirector>();
  serpentCamera_->Initialize();
  bossCamera_ = std::make_unique<BossCameraDirector>();

  // 評価
  scoreConfig_.Load();
  scoreKeeper_ = std::make_unique<ScoreKeeper>();
  hud_ = std::make_unique<GameHud>();
  hud_->Initialize(engine_->GetSpriteRenderer());
  resultSequence_ = std::make_unique<ResultSequence>();
  resultSequence_->Initialize(engine_->GetSpriteRenderer());
  pauseMenu_ = std::make_unique<PauseMenu>();
  pauseMenu_->Initialize(engine_->GetSpriteRenderer());
  skipHold_ = std::make_unique<SkipHold>();
  skipHold_->Initialize(engine_->GetSpriteRenderer());

  //===========================
  // プレイヤーの初期化
  //===========================
  ModelManager::GetInstance()->LoadModel("suzanne.obj");

  player_ = std::make_unique<Player>(railCamera_.get());
  player_->SetSpriteRenderer(engine_->GetSpriteRenderer());
  player_->SetObject3dRenderer(engine_->GetObject3dRenderer());
  player_->SetInput(engine_->GetInputManager());
  // 自機の被弾: ヒットストップをかけてカメラを揺らす
  player_->SetOnDamagedCallback([this]() {
    RequestHitStop(kPlayerDamagedHitStopFrames_);
    railCamera_->Shake(kPlayerDamagedShakeIntensity_, kPlayerDamagedShakeSeconds_);
  });
  player_->SetScoreKeeper(scoreKeeper_.get());

  // プレイヤーの初期化（照準やコライダーの生成など）
  player_->Initialize();
  auto dragonModel = std::make_unique<DragonModel>();
  dragonModel->Initialize(engine_->GetObject3dRenderer(),
                          engine_->GetSrvManager());
  player_->SetDragonModel(std::move(dragonModel));

  cameraTransform_ = {
      {1.0f, 1.0f, 1.0f},
      {0.3f, 0.0f, 0.0f},
      {0.0f, 4.0f, -10.0f},
  };

  // デバッグカメラ
  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize({0.0f, 10.0f, -30.0f});

  // レベルデータのロード
  LoadLevel();

  // スプラインデータのロード
  LoadSplines();

  // UIの読み込み
  UIManager::GetInstance()->Load("resources/UI/GamePlayUI.json");

  // リボントレイル用テクスチャのロード
  TextureManager::GetInstance()->LoadTexture("resources/gradationLine.png");
}

void GamePlayScene::Finalize() {
  SoundManager::GetInstance()->StopBGM();
  SoundManager::GetInstance()->SetBGMVolume(1.0f);
  // 平行光源は全シーン共通なので、このシーンで足した環境光と逆光の縁を外しておく
  StageLighting::ResetSharedLight(engine_->GetObject3dRenderer());
}

void GamePlayScene::Update() {

  bool isPlayMode_ = GameManager::GetInstance()->IsGlobalPlayMode();

  if (isPlayMode_ && !previousGlobalPlayMode_) {
    LoadSplines();
    SaveLevel("level_editor_temp.json");
    isPaused_ = false;
    useDebugCamera_ = false;

    if (railCamera_) {
      railCamera_->SetAutoMove(true);
      railCamera_->SetSpeed(0.2f); // 速度リセット
      playStartT_ = railCamera_->GetT();
    }
    for (auto &ev : spawnEvents_) {
      if (ev.spawnTime < playStartT_) {
        ev.hasSpawned = true; // 過去のイベントはスキップ
      } else {
        ev.hasSpawned = false;
      }
    }

    // 点と撃墜率を数え直す
    scoreKeeper_->Reset(&scoreConfig_, GetBossSpawnTime());
    hud_->Reset();

    if (playIntroOnStart_) {
      ChangeState(std::make_unique<IntroState>(*this));
    }
  } else if (!isPlayMode_ && previousGlobalPlayMode_) {
    isPaused_ = false;
    useDebugCamera_ = true;
    LoadLevel("level_editor_temp.json");

    // ゲーム状態のリセット
    ChangeState(std::make_unique<PlayState>(*this));
    pauseMenu_->Close();
    SoundManager::GetInstance()->SetBGMVolume(1.0f);
    stageIntro_->Stop();
    gameOverSequence_->Stop();
    clearSequence_->Stop();
    resultSequence_->Stop();
    railCamera_->SetSpeedScale(1.0f);
    player_->SetExternalWingState(std::nullopt);
    player_->SetWingGesture({});
    player_->SetDirectWingRoot(nullptr);
    UIManager::GetInstance()->Load("resources/UI/GamePlayUI.json");

    // 残っている敵や弾をクリア
    ClearRuntimeEnemies();
    ActorManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->Clear(); // コライダー残留バグ対策

    // プレイヤーのステータスを初期化
    if (player_) {
      player_->SetLoadConfigOnInitialize(false);
      player_->Initialize();
      player_->SetReticleAlpha(1.0f);
      player_->SetBackRimScale(0.0f);
    }
    hudFadeTimer_ = hudFadeSec_;

    if (railCamera_) {
      railCamera_->SetT(playStartT_);
      bool autoMoveCache = railCamera_->GetAutoMove();
      railCamera_->SetAutoMove(false);
      railCamera_->Update();
      railCamera_->SetAutoMove(autoMoveCache);
      for (auto &ev : spawnEvents_) {
        ev.hasSpawned = false;
      }
    }
    if (player_) {
      player_->ForceSnapToCamera();
    }
  }
  previousGlobalPlayMode_ = isPlayMode_;

#ifdef USE_IMGUI
  // Undo/Redo ショートカット (Ctrl + Z / Ctrl + Y)
  // Playモード中は編集操作のUndo/Redoを禁止する
  if (!isPlayMode_ && (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ||
                       ImGui::IsKeyDown(ImGuiKey_RightCtrl))) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      CommandManager::GetInstance()->Undo();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      CommandManager::GetInstance()->Redo();
    }
  }
#endif

  // Sound更新
  SoundManager::GetInstance()->Update();

  // UI更新
  Input *input = GameManager::GetInstance()->IsGlobalPlayMode()
                     ? engine_->GetInputManager()
                     : nullptr;
  UIManager::GetInstance()->Update(input);

  // デバッグカメラ切り替え
  if (engine_->GetInputManager()->IsTriggerKey(DIK_P)) {
    if (useDebugCamera_) {
      useDebugCamera_ = false;
    } else {
      useDebugCamera_ = true;
    }
  }

  // ポーズ画面。開いている間は、エディタの一時停止と同じに世界の時間を止める
  const bool isPauseMenuOpen = UpdatePauseMenu(input);
  const bool isWorldPaused = isPaused_ || isPauseMenuOpen;

  //=======================
  // ゲーム進行状態の更新
  //=======================
  // 今の状態を更新し、次の状態が返ってきたら入れ替える（ポーズ画面を開いている間は移らない）
  if (!isPauseMenuOpen) {
    if (std::unique_ptr<GamePlayState> next = state_->Update()) {
      ChangeState(std::move(next));
    }
  }

  if (hitStopTimer_ > 0) {
    hitStopTimer_--;
  }
  if (hitStopCooldownTimer_ > 0) {
    hitStopCooldownTimer_--;
  }

  // レール・敵・弾・当たり判定を進めるか（止めるかは今の状態が決める）
  bool shouldUpdateWorld =
      ((isPlayMode_ && !isWorldPaused && hitStopTimer_ <= 0) || doStep_) &&
      state_->UpdatesWorld();

  // 世界の時間がゆっくりな状態では、時間の速さに合わせて、進めるフレームを間引く
  const float worldTimeScale = state_->GetWorldTimeScale();
  if (shouldUpdateWorld && worldTimeScale < 1.0f) {
    worldTimeCarry_ += worldTimeScale;
    if (worldTimeCarry_ >= 1.0f) {
      worldTimeCarry_ -= 1.0f;
    } else {
      shouldUpdateWorld = false;
    }
  }

  if (doStep_) {
    doStep_ = false;
  }

  //=======================
  // カメラの更新
  //=======================
  const ICamera *activeCamera = nullptr;

  // RailCameraにプレイヤーの現在位置（前フレーム座標）を渡す
  // 開始演出中は操作開始時の位置を渡し、レールカメラの向きを固定しておく
  if (player_ && railCamera_) {
    if (state_->HoldsRailCameraAim()) {
      railCamera_->SetPlayerWorldPosition(stageIntro_->GetDragonEndPosition());
    } else {
      railCamera_->SetPlayerWorldPosition(player_->GetTransform().translate);
    }
  }

  if (useDebugCamera_) {
    debugCamera_->Update(*engine_->GetInputManager());
    activeCamera = debugCamera_->GetCamera();
  } else {
    // 世界が止まっていても、今の状態が動かすならレールを動かす
    const bool isRailMoving =
        shouldUpdateWorld ||
        (isPlayMode_ && !isWorldPaused && state_->MovesRailWhileWorldStopped());
    if (!isRailMoving) {
      bool autoMoveCache = railCamera_->GetAutoMove();
      railCamera_->SetAutoMove(false);
      railCamera_->Update();
      railCamera_->SetAutoMove(autoMoveCache);
    } else {
      railCamera_->Update();
    }
    activeCamera = railCamera_.get();
  }

  // 開始演出：自機の位置を動かし、演出カメラで映す（開始演出の状態のあと、操作へ移る間も演出カメラで映す）
  if (!stageIntro_->IsFinished()) {
    if (isPlayMode_ && !isWorldPaused) {
      UpdateIntro();
    }
    if (!useDebugCamera_) {
      activeCamera = stageIntro_->GetCamera();
    }
  }

  // 今の状態の演出を進め、演出があれば演出カメラで映す
  if (const ICamera *sequenceCamera =
          state_->UpdateSequence(isPlayMode_ && !isWorldPaused)) {
    if (!useDebugCamera_) {
      activeCamera = sequenceCamera;
    }
  }

  // 目標カード
  if (isPlayMode_ && !isWorldPaused) {
    stageIntro_->UpdateCaption();
  }

  // 咆哮の空間のゆがみ（開始演出が終わったあとは「ゆがみだけ再生」のときだけ動く）
  if (postProcess_ && player_) {
    std::optional<float> timeSinceRoar;
    if (!stageIntro_->IsFinished()) {
      timeSinceRoar = stageIntro_->GetTimeSinceRoar();
    }
    roarDistortion_->Update(timeSinceRoar, player_->GetHeadPosition(),
                            activeCamera, postProcess_.get());
  }

  // ポストエフェクトとエフェクトマネージャーの更新（ポーズ画面を開いている間は止める）
  if (postProcess_ && !isPauseMenuOpen) {
    EffectManager::GetInstance()->Update(activeCamera);
  }

  // スポナーロジック
  if (railCamera_) {
    float t = railCamera_->GetT();
    for (auto &ev : spawnEvents_) {
      if (t < ev.spawnTime) {
        ev.hasSpawned = false; // シークバック時にフラグをリセット
      } else if (shouldUpdateWorld && !ev.hasSpawned && t >= ev.spawnTime) {
        // 名前に合う出し方で出す（Playモード時のみ）
        if (isPlayMode_) {
          AddSpawned(SelectSpawner(ev.prefabName).Spawn(ev, MakeSpawnContext()));
        }
        ev.hasSpawned = true;
      }
    }
  }

  // 基底クラスにも現在のアクティブカメラを教える（Gizmo描画などで使うため）
  SetActiveCamera(const_cast<ICamera *>(activeCamera));

  // 砦と大蛇を動かしてから、それらに付いた部品を含む敵を動かす
  for (auto &fortress : fortresses_) {
    if (shouldUpdateWorld) {
      fortress->Update();
    } else {
      fortress->UpdateTransform();
    }
    // 砦が倒されたら時間を止め、大爆発したらカメラを揺らす
    for (FloatingFortress::Event event : fortress->TakeEvents()) {
      if (event == FloatingFortress::Event::Killed) {
        RequestHitStop(hitStopBossPartFrames_);
      } else if (event == FloatingFortress::Event::Exploded) {
        railCamera_->Shake(kFortressDestroyedShakeIntensity_, kFortressDestroyedShakeSeconds_);
      }
    }
  }
  for (auto &serpent : serpents_) {
    if (shouldUpdateWorld) {
      serpent->Update();
    } else {
      serpent->UpdateTransform();
    }
    // 大蛇の出来事でカメラとレールを動かす
    for (MechSerpent::Event event : serpent->TakeEvents()) {
      serpentCamera_->OnEvent(event, *serpent, *railCamera_);
    }
  }

  // 敵の更新 (Playモードで生成された敵のみ)
  for (auto &enemy : runtimeEnemies_) {
    if (shouldUpdateWorld) {
      enemy->Update();
    } else {
      enemy->UpdateTransform();
    }
  }
  // ボスの出来事でカメラを動かす（世界が止まっている間は動かさない）
  if (shouldUpdateWorld) {
    Boss *boss = boss_.Get();
    if (boss) {
      for (Boss::CameraEvent event : boss->TakeEvents()) {
        bossCamera_->OnEvent(event, *railCamera_);
      }
    }
    bossCamera_->Update(boss, *railCamera_);
  }
  for (auto &obj : sceneObjects_) {
    obj->Update();
  }

  // LockOn用にPlayerに対象リストを渡す（毎回最新の状態を渡す）
  // 死亡済みの敵は除外してダングリングポインタを渡さないようにする
  std::vector<BaseActor *> lockOnTargets;
  for (auto &e : runtimeEnemies_) {
    if (!e->IsDead() && e->CanBeLockedOn()) {
      lockOnTargets.push_back(e.get());
    }
  }

  // ActorManager が持つ物（ボスの部品・敵の弾など）からも、今ロックオンできる物を加える
  std::vector<BaseActor *> managedTargets =
      ActorManager::GetInstance()->FindActorsIf([](BaseActor *actor) {
        auto *hittable = dynamic_cast<ILockOnHittable *>(actor);
        return hittable && hittable->CanBeLockedOn();
      });
  lockOnTargets.insert(lockOnTargets.end(), managedTargets.begin(),
                       managedTargets.end());

  // 敵を隠す箱。箱のモデルは -0.5〜0.5 の立方体なので、ワールド行列の逆で箱の中の座標へ移し、
  // 2 倍して LockOn が使う -1〜1 の立方体にそろえる
  lockOnOccluders_.clear();
  const Matrix4x4 toUnitCube = MakeScaleMatrix({2.0f, 2.0f, 2.0f});
  for (const auto &obj : sceneObjects_) {
    if (obj->GetModelPath() == kOccluderModelPath_ ||
        obj->GetModelPath() == kTowerPanelModelPath_) {
      lockOnOccluders_.push_back(
          Multiply(Inverse(obj->GetWorldMatrix()), toUnitCube));
    }
  }
  for (const auto &fortress : fortresses_) {
    fortress->AppendOccluders(lockOnOccluders_);
  }

  if (player_) {
    player_->SetLockOnTargets(lockOnTargets);
    player_->SetShotTargets(
        ActorManager::GetInstance()->FindActorsWithTag(ActorTag::ShotTarget));
    player_->SetLockOnOccluders(&lockOnOccluders_);
  }

  // 死亡済みの敵を削除（デストラクタ内でコライダーも自動登録解除される）
  if (shouldUpdateWorld) {
    runtimeEnemies_.erase(std::remove_if(runtimeEnemies_.begin(),
                                         runtimeEnemies_.end(),
                                         [](const std::unique_ptr<Enemy> &e) {
                                           return e->IsDead();
                                         }),
                          runtimeEnemies_.end());
    fortresses_.erase(std::remove_if(fortresses_.begin(), fortresses_.end(),
                                     [](const std::unique_ptr<FloatingFortress> &f) {
                                       return f->IsFinished();
                                     }),
                      fortresses_.end());
    serpents_.erase(std::remove_if(serpents_.begin(), serpents_.end(),
                                   [](const std::unique_ptr<MechSerpent> &s) {
                                     return s->IsFinished();
                                   }),
                    serpents_.end());
  }

  // アクティブカメラを描画で使用する
  engine_->GetObject3dRenderer()->SetDefaultCamera(activeCamera);

  if (skybox_) {
    // ポーズ画面を開いている間は、空と雲海の時間も止める
    const float kSkyTimeStep = isPauseMenuOpen ? 0.0f : 1.0f / 60.0f;
    skyTimer_ += kSkyTimeStep;
    // 開始演出の間は演出が出来事の時刻を決め、それ以外はここで進める
    if (stageIntro_->IsFinished()) {
      cloudSeaEvents_->Advance(kSkyTimeStep);
    }
    ApplySkyAndLighting();
    skybox_->SetCamera(activeCamera);
    skybox_->SetDebugView(drawToggles_.skyDebugView);
    skybox_->Update();
    cloudSea_->SetTime(skyTimer_);
    cloudSea_->SetCamera(activeCamera);
    cloudSea_->Update();
  }

  //===========================================
  // プレイヤーの更新
  //===========================================

  if (player_) {
    // プレイヤーの照準や挙動の計算には常にレールカメラを使用する
    // 操作を受けない状態（クリアしたあと）では、位置と見た目だけ更新する
    if (shouldUpdateWorld && state_->AcceptsPlayerControl()) {
      player_->Update();
      // 開始演出から操作へ移る間は、演出が決める位置・向きと操作の位置・向きを混ぜる
      if (!stageIntro_->IsFinished()) {
        BlendPlayerWithIntro();
      }
    } else {
      player_->UpdateTransform();
    }
    // 迎撃の合図は、レールカメラの画面に描く。時間は世界と一緒に進める
    if (shouldUpdateWorld) {
      interceptCue_->Update(railCamera_->GetViewProjectionMatrix());
    }
    // 狙われている間は、合図の拍に合わせて自機の体を赤く光らせる
    player_->SetThreatGlow(interceptCue_->GetThreat());
    // 演出が自機を動かしている間は、演出が決めた位置・向きで見た目を更新する
    if (state_->IsPlayerPosedBySequence()) {
      player_->UpdateTransform();
    }
    // 羽ばたきを進めるかは今の状態が決める
    const bool isSequenceMoving = isPlayMode_ && !isPaused_;
    const float wingDeltaTime =
        state_->MovesWing(shouldUpdateWorld, isSequenceMoving) ? kWingDeltaTime_ : 0.0f;
    // 竜の力の光（模様の光と傷）。ゲームオーバーの間は演出が強さを決める
    if (DragonModel *dragonModel = player_->GetDragonModel()) {
      dragonModel->SetGlow(gameOverSequence_->GetDragonGlow());
    }
    player_->UpdateWing(wingDeltaTime);
  }

  // アクター群の更新
  if (shouldUpdateWorld) {
    ActorManager::GetInstance()->Update();
  }

  // 当たり判定の更新
  if (shouldUpdateWorld) {
    CollisionManager::GetInstance()->Update();
  }

  // HUD（開始演出のあとに現し、ゲームオーバーでは被弾から消していく。ポーズ画面を開いている間は止める）
  if (player_ && railCamera_ && !isPauseMenuOpen) {
    const float playHudAlpha = hudFadeSec_ > 0.0f ? hudFadeTimer_ / hudFadeSec_ : 1.0f;
    const float hudAlpha = state_->GetHudAlpha(playHudAlpha);
    hud_->Update(*player_, *scoreKeeper_, boss_.Get(),
                 railCamera_->GetViewProjectionMatrix(), hudAlpha);
  }

#ifdef USE_IMGUI
  //=========================
  // メインメニューバー
  //=========================
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save Level")) {
        SaveLevel();
      }
      if (ImGui::MenuItem("Load Level")) {
        LoadLevel();
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  //=========================
  // Hierarchy ウィンドウ
  //=========================
  ImGui::Begin("Hierarchy");
  ImGui::Text("Scene Objects");
  ImGui::Separator();

  if (ImGui::Selectable("Player",
                        currentSelectType_ == EditorSelectType::Player)) {
    currentSelectType_ = EditorSelectType::Player;
  }
  if (ImGui::Selectable("Environment",
                        currentSelectType_ == EditorSelectType::Environment)) {
    currentSelectType_ = EditorSelectType::Environment;
  }
  if (ImGui::Selectable("Effect (Shockwave)",
                        currentSelectType_ == EditorSelectType::Effect)) {
    currentSelectType_ = EditorSelectType::Effect;
  }
  ImGui::Separator();
  bool isRailCameraOpen = ImGui::TreeNodeEx(
      "Rail Camera", ImGuiTreeNodeFlags_OpenOnArrow |
                         ImGuiTreeNodeFlags_OpenOnDoubleClick |
                         (currentSelectType_ == EditorSelectType::RailCamera &&
                                  selectedWaypointIndex_ == -1
                              ? ImGuiTreeNodeFlags_Selected
                              : 0));
  if (ImGui::IsItemClicked()) {
    currentSelectType_ = EditorSelectType::RailCamera;
    selectedWaypointIndex_ = -1;
  }
  if (isRailCameraOpen) {
    if (railCamera_) {
      auto &waypoints = railCamera_->GetWaypointsRef();
      for (size_t i = 0; i < waypoints.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        std::string wpLabel = "Waypoint " + std::to_string(i);
        bool wpSelected = (currentSelectType_ == EditorSelectType::RailCamera &&
                           selectedWaypointIndex_ == static_cast<int>(i));
        if (ImGui::Selectable(wpLabel.c_str(), wpSelected)) {
          currentSelectType_ = EditorSelectType::RailCamera;
          selectedWaypointIndex_ = static_cast<int>(i);
        }
        ImGui::PopID();
      }
    }
    ImGui::TreePop();
  }

  ImGui::Spacing();
  ImGui::Text("Spawn Events (Timeline)");
  ImGui::Separator();

  // タイムラインピンのリストを表示
  for (size_t i = 0; i < spawnEvents_.size(); ++i) {
    char label[128];
    snprintf(label, sizeof(label), "[%zu] %s (t=%.2f)", i,
             spawnEvents_[i].prefabName.c_str(), spawnEvents_[i].spawnTime);

    bool isSelected = (currentSelectType_ == EditorSelectType::SpawnEvent &&
                       selectedSpawnEventIndex_ == static_cast<int>(i));
    if (ImGui::Selectable(label, isSelected)) {
      SelectSpawnEvent(static_cast<int>(i));
    }
  }

  ImGui::Spacing();
  if (ImGui::TreeNodeEx("Prefabs (Master Data)",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
    if (std::filesystem::exists("resources/prefabs")) {
      for (const auto &entry :
           std::filesystem::directory_iterator("resources/prefabs")) {
        if (entry.path().extension() == ".prefab") {
          std::string name = entry.path().stem().string();
          bool isSelected = (currentSelectType_ == EditorSelectType::Prefab &&
                             selectedPrefabName_ == name);
          if (ImGui::Selectable(name.c_str(), isSelected)) {
            currentSelectType_ = EditorSelectType::Prefab;
            if (selectedPrefabName_ != name) {
              selectedPrefabName_ = name;
              tempPrefabEditEnemy_ =
                  PrefabManager::GetInstance()->InstantiateEnemy(name,
                                                                 Transform());
            }
          }
        }
      }
    } else {
      ImGui::TextDisabled("No prefabs found.");
    }
    ImGui::TreePop();
  }

  ImGui::Spacing();
  ImGui::Text("Scene Objects (Dynamic)");
  ImGui::Separator();
  for (size_t i = 0; i < sceneObjects_.size(); ++i) {
    std::string label = "Object " + std::to_string(i) + " (" +
                        sceneObjects_[i]->GetModelPath() + ")";
    bool isSelected = (currentSelectType_ == EditorSelectType::SceneObject &&
                       selectedSceneObjectIndex_ == static_cast<int>(i));
    if (ImGui::Selectable(label.c_str(), isSelected)) {
      currentSelectType_ = EditorSelectType::SceneObject;
      selectedSceneObjectIndex_ = static_cast<int>(i);
    }
  }

  ImGui::End();

  //=========================
  // Inspector ウィンドウ
  //=========================
  ImGui::Begin("Inspector");
  ImGui::SetWindowFontScale(0.85f);

  if (currentSelectType_ == EditorSelectType::Player) {
    ImGui::Text("Player Action Settings");
    ImGui::Separator();
    if (player_) {
      player_->DrawActionConfigImGui();
      // 自機が画面のどこに映っているか（画面は 1280x720、中央は y=360）
      {
        const ICamera *shownCamera = GetActiveCamera() ? GetActiveCamera() : railCamera_.get();
        const Vector2 screen = WorldToScreen(player_->GetTransform().translate,
                                             shownCamera->GetViewProjectionMatrix(), 1280.0f, 720.0f);
        const Vector2 offset = player_->GetFramingOffset();
        ImGui::Text((const char *)u8"自機の画面の位置 x %.0f  y %.0f（中央 640, 360）", screen.x, screen.y);
        ImGui::Text((const char *)u8"構図の中の位置 右 %.2f  上 %.2f m", offset.x, offset.y);
        ImGui::Text((const char *)u8"映しているカメラ: %s  開始演出 %s（操作の効き %.2f）",
                    shownCamera == railCamera_.get() ? "Rail" : "Other",
                    stageIntro_->IsFinished() ? (const char *)u8"終わり" : (const char *)u8"途中",
                    stageIntro_->GetControlRatio());
      }
    } else {
      ImGui::Text("Player is not active.");
    }
  } else if (currentSelectType_ == EditorSelectType::Environment) {
    ImGui::Text("Environment Settings");
    ImGui::Separator();
    auto pp = SceneManager::GetInstance()->GetCurrentScenePostProcess();
    if (pp) {
      pp->DrawDebugUI("Environment", false); // インライン描画
    } else {
      ImGui::Text("No PostProcess active.");
    }
  } else if (currentSelectType_ == EditorSelectType::Effect) {
    EffectManager::GetInstance()->DrawEditorUI(railCamera_.get());
    FractureCache::GetInstance()->DrawEditorUI();
  } else if (currentSelectType_ == EditorSelectType::RailCamera) {
    DrawLookKeysUI();
    serpentCamera_->DrawImGui();

    ImGui::Text("Rail Camera Waypoints");
    ImGui::Separator();

    if (railCamera_) {
      auto &waypoints = railCamera_->GetWaypointsRef();
      for (size_t i = 0; i < waypoints.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("Point %zu", i);
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
          waypoints.erase(waypoints.begin() + i);
          ImGui::PopID();
          break; // 安全のため1フレームに1つだけ削除
        }
        ImGui::DragFloat3("##Pos", &waypoints[i].x, 0.1f);
        ImGui::PopID();
      }
      if (ImGui::Button("Add Waypoint")) {
        if (!waypoints.empty()) {
          waypoints.push_back(waypoints.back() + Vector3{0, 0, 10.0f});
        } else {
          waypoints.push_back({0, 0, 0});
        }
      }
    }
  } else if (currentSelectType_ == EditorSelectType::SceneObject &&
             selectedSceneObjectIndex_ >= 0 &&
             selectedSceneObjectIndex_ < sceneObjects_.size()) {
    Object3d *selected = sceneObjects_[selectedSceneObjectIndex_].get();
    ImGui::Text("Scene Object %d", selectedSceneObjectIndex_);
    ImGui::TextDisabled("%s", selected->GetModelPath().c_str());
    ImGui::Separator();

    Vector3 t = selected->GetTranslation();
    Vector3 r = selected->GetRotation();
    Vector3 s = selected->GetScale();
    if (ImGui::DragFloat3("Translate", &t.x, 0.1f))
      selected->SetTranslation(t);
    if (ImGui::DragFloat3("Rotate", &r.x, 0.01f))
      selected->SetRotation(r);
    if (ImGui::DragFloat3("Scale", &s.x, 0.1f))
      selected->SetScale(s);
    ImGui::Separator();

    Vector4 color = selected->GetColor();
    float specularScale = selected->GetSpecularScale();
    if (ImGui::ColorEdit3("Color", &color.x))
      selected->SetColor(color);
    if (ImGui::SliderFloat("Specular", &specularScale, 0.0f, 1.0f))
      selected->SetSpecularScale(specularScale);
  } else if (currentSelectType_ == EditorSelectType::SpawnEvent &&
             selectedSpawnEventIndex_ >= 0 &&
             selectedSpawnEventIndex_ < spawnEvents_.size()) {
    SpawnEvent &ev = spawnEvents_[selectedSpawnEventIndex_];

    // 編集前状態の保存用
    static SpawnEvent s_editStartState;
    static int s_lastSelectedIndex = -1;
    bool isAnyEditActive = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();

    if (!isAnyEditActive || s_lastSelectedIndex != selectedSpawnEventIndex_) {
      s_editStartState = ev;
    }
    s_lastSelectedIndex = selectedSpawnEventIndex_;
    bool editFinished = false;

    ImGui::Text("Spawn Event %d", selectedSpawnEventIndex_);
    ImGui::Separator();

    // 最大時間（レール終点）を取得
    float maxT = 0.0f;
    if (railCamera_ && railCamera_->GetWaypoints().size() > 0) {
      maxT = static_cast<float>(railCamera_->GetWaypoints().size() - 1);
    }

    ImGui::DragFloat("Time", &ev.spawnTime, 0.01f, 0.0f, maxT);
    if (ImGui::IsItemDeactivatedAfterEdit())
      editFinished = true;

    // プレハブのコンボボックス
    std::vector<std::string> availablePrefabs;
    if (std::filesystem::exists("resources/prefabs")) {
      for (const auto &entry :
           std::filesystem::directory_iterator("resources/prefabs")) {
        if (entry.path().extension() == ".prefab") {
          availablePrefabs.push_back(entry.path().stem().string());
        }
      }
    }

    if (ImGui::BeginCombo("Prefab", ev.prefabName.c_str())) {
      for (const auto &pName : availablePrefabs) {
        bool isSelected = (ev.prefabName == pName);
        if (ImGui::Selectable(pName.c_str(), isSelected)) {
          if (ev.prefabName != pName) {
            ev.prefabName = pName;
            editFinished = true;
          }
        }
        if (isSelected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    if (ImGui::DragFloat3("Spawn Offset", &ev.spawnOffset.x, 0.1f)) {
      editFinished = true;
    }

    const char *moveTypes[] = {"Straight (直進)",   "Parallel (平行移動)",
                               "SineWave (波打ち)", "Stationary (静止)",
                               "Fighter (戦闘機)",  "Meteor (メテオ突撃)",
                               "Strafe (画面横断)", "Turret (固定砲台)"};
    int currentMoveType = static_cast<int>(ev.moveType);
    if (ImGui::Combo("Move Type", &currentMoveType, moveTypes,
                     IM_ARRAYSIZE(moveTypes))) {
      ev.moveType = static_cast<MoveType>(currentMoveType);
      editFinished = true;
    }

    if (ImGui::DragFloat3("Move Direction", &ev.moveDirection.x, 0.05f)) {
      editFinished = true;
    }

    // レール選択コンボボックス
    if (ImGui::BeginCombo("Rail Spline", ev.splineName.empty()
                                             ? "None (Straight)"
                                             : ev.splineName.c_str())) {
      // 「なし」を選択できるようにする
      bool isNoneSelected = ev.splineName.empty();
      if (ImGui::Selectable("None (Straight)", isNoneSelected)) {
        if (!ev.splineName.empty()) {
          ev.splineName = "";
          editFinished = true;
        }
      }
      if (isNoneSelected) {
        ImGui::SetItemDefaultFocus();
      }

      // ロード済みのスプラインを一覧表示
      for (const auto &pair : loadedSplines_) {
        const std::string &sName = pair.first;
        bool isSelected = (ev.splineName == sName);
        if (ImGui::Selectable(sName.c_str(), isSelected)) {
          if (ev.splineName != sName) {
            ev.splineName = sName;
            editFinished = true;
          }
        }
        if (isSelected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    // レールが選択されている場合のみ、時間と座標系の設定を表示
    if (!ev.splineName.empty()) {
      if (ImGui::SliderFloat("Rail Duration (sec)", &ev.splineDuration, 0.5f,
                             30.0f, "%.1f")) {
        // ドラッグ中は都度保存はしないが、離した時にセーブされるようにする
      }
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        editFinished = true;
      }

      if (ImGui::Checkbox("World Space Spline", &ev.isWorldSpaceSpline)) {
        editFinished = true;
      }

      if (ImGui::InputInt("Fire Interval (frames)", &ev.fireInterval)) {
        if (ev.fireInterval < -1)
          ev.fireInterval = -1; // -1を最小値とする
        editFinished = true;
      }
    }

    if (ImGui::Checkbox("World Space Position", &ev.isWorldSpace)) {
      editFinished = true;
    }

    Vector3 worldPos;
    if (ev.isWorldSpace) {
      worldPos = ev.spawnOffset;
    } else if (railCamera_) {
      Vector3 rawCamPos = railCamera_->CalcPosition(ev.spawnTime);
      Vector3 tangent = railCamera_->CalcTangent(ev.spawnTime);
      Vector3 worldUp = {0.0f, 1.0f, 0.0f};
      Vector3 right = SafeNormalize(Cross(worldUp, tangent));
      Vector3 up = SafeNormalize(Cross(tangent, right));
      Vector3 forward = tangent;

      Vector3 camActualPos = rawCamPos;
      camActualPos.y += 2.5f;

      worldPos =
          camActualPos +
          Vector3{right.x * ev.spawnOffset.x, right.y * ev.spawnOffset.x,
                  right.z * ev.spawnOffset.x} +
          Vector3{up.x * ev.spawnOffset.y, up.y * ev.spawnOffset.y,
                  up.z * ev.spawnOffset.y} +
          Vector3{forward.x * ev.spawnOffset.z, forward.y * ev.spawnOffset.z,
                  forward.z * ev.spawnOffset.z};
    }

    ImGui::Spacing();
    ImGui::TextDisabled("World Pos: (%.1f, %.1f, %.1f)", worldPos.x, worldPos.y,
                        worldPos.z);

    // MoveType はプレハブ側の設定に移動したため、ここには表示しない

    if (editFinished) {
      CommandManager::GetInstance()->ExecuteCommand(
          std::make_unique<CmdModifySpawnEvent>(this, selectedSpawnEventIndex_,
                                                s_editStartState, ev));
      s_editStartState = ev;
    }

    ImGui::Spacing();
    if (ImGui::Button("Delete Event", ImVec2(-1, 0))) {
      CommandManager::GetInstance()->ExecuteCommand(
          std::make_unique<CmdDeleteSpawnEvent>(this,
                                                selectedSpawnEventIndex_));
    }
  } else if (currentSelectType_ == EditorSelectType::Prefab &&
             tempPrefabEditEnemy_) {
    ImGui::Text("Prefab Master Settings: %s", selectedPrefabName_.c_str());
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
    ImGui::PopStyleColor();
    ImGui::Spacing();

    bool changed = false;

    int hp = tempPrefabEditEnemy_->GetHP();
    if (ImGui::InputInt("HP (体力)", &hp)) {
      if (hp < 1)
        hp = 1;
      tempPrefabEditEnemy_->SetHP(hp);
      changed = true;
    }

    float speed = tempPrefabEditEnemy_->GetSpeed();
    if (ImGui::DragFloat("Speed (移動速度)", &speed, 0.01f, 0.0f, 10.0f)) {
      tempPrefabEditEnemy_->SetSpeed(speed);
      changed = true;
    }

    Vector3 dir = tempPrefabEditEnemy_->GetMoveDirection();
    if (ImGui::DragFloat3("Move Direction (移動方向)", &dir.x, 0.01f)) {
      tempPrefabEditEnemy_->SetMoveDirection(dir);
      changed = true;
    }

    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
    if (ImGui::Button((const char *)u8"Save Prefab (上書き保存)",
                      ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
      PrefabManager::GetInstance()->SavePrefab(selectedPrefabName_,
                                               tempPrefabEditEnemy_.get());
    }
    ImGui::PopStyleColor();
  } else {
    ImGui::Text("No object selected.");
  }
  ImGui::End();

  //=========================
  // Timeline & Sequencer UI
  //=========================
  ImGui::Begin("Timeline & Sequencer");

  ImGui::Checkbox("Debug Camera [P]", &useDebugCamera_);
  if (ImGui::TreeNode("Draw Toggles")) {
    ImGui::Checkbox("Sky", &drawToggles_.sky);
    ImGui::Checkbox("Scene Objects", &drawToggles_.sceneObjects);
    ImGui::Checkbox("Enemies", &drawToggles_.enemies);
    ImGui::Checkbox("Clear Light", &drawToggles_.clearLight);
    ImGui::Checkbox("Player", &drawToggles_.player);
    ImGui::Checkbox("Actors", &drawToggles_.actors);
    ImGui::Checkbox("Cloud Sea", &drawToggles_.cloudSea);
    ImGui::Checkbox("Particles", &drawToggles_.particles);
    ImGui::Checkbox("Trails", &drawToggles_.trails);
    ImGui::Checkbox("UI", &drawToggles_.ui);
    ImGui::Checkbox("Rail Line", &drawToggles_.railLine);
    ImGui::Checkbox("Flight Path", &drawToggles_.flightPath);
    // クリア後のドラゴンが飛ぶ所を、デバッグカメラで真上から見下ろす
    if (ImGui::Button("Flight Top View")) {
      Vector3 translate;
      Vector3 rotate;
      if (clearSequence_->GetTopView(translate, rotate)) {
        debugCamera_->SetTranslate(translate);
        debugCamera_->SetRotate(rotate);
        useDebugCamera_ = true;
        drawToggles_.flightPath = true;
      }
    }
    const char *const kSkyViews[] = {"Normal", "Direction", "Cloud Noise",
                                     "Clear Ratio"};
    ImGui::Combo("Sky View", &drawToggles_.skyDebugView, kSkyViews,
                 IM_ARRAYSIZE(kSkyViews));
    ImGui::TreePop();
  }

  float currentT = railCamera_->GetT();
  float maxT = static_cast<float>(railCamera_->GetWaypoints().size() - 1);
  if (maxT < 0.0f)
    maxT = 0.0f;
  // Playモード中はスライダーの操作を無効化
  if (isPlayMode_) {
    ImGui::BeginDisabled();
  }

  if (ImGui::SliderFloat("Rail Progress (t)", &currentT, 0.0f, maxT)) {
    railCamera_->SetT(currentT);

    // レールカメラの位置を即座に更新してプレイヤーを追従させる
    bool autoMoveCache = railCamera_->GetAutoMove();
    railCamera_->SetAutoMove(false);
    railCamera_->Update();
    railCamera_->SetAutoMove(autoMoveCache);

    // スライダーを直接動かした時は自動再生をオフにする（ポーズ状態にする）
    isPaused_ = true;
    if (player_) {
      player_->ForceSnapToCamera();
    }
  }

  if (isPlayMode_) {
    ImGui::EndDisabled();
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float trackWidth = ImGui::GetContentRegionAvail().x;
  float trackHeight = 40.0f;

  // 背景描画
  drawList->AddRectFilled(p, ImVec2(p.x + trackWidth, p.y + trackHeight),
                          IM_COL32(50, 50, 50, 255));

  // マウス操作判定用の非表示ボタン
  ImGui::InvisibleButton("TimelineTrack", ImVec2(trackWidth, trackHeight));
  bool isTrackHovered = ImGui::IsItemHovered();

  // ダブルクリックで新規スポーンイベント追加
  if (isTrackHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    float mouseX = ImGui::GetMousePos().x - p.x;
    float t = (mouseX / trackWidth) * maxT;
    SpawnEvent ev;
    ev.spawnTime = railCamera_ ? railCamera_->GetT() : 0.0f;
    ev.prefabName = "ZakoEnemy";
    ev.spawnOffset = {0.0f, 0.0f, 50.0f};

    int newIndex = static_cast<int>(spawnEvents_.size());
    CommandManager::GetInstance()->ExecuteCommand(
        std::make_unique<CmdAddSpawnEvent>(this, ev, newIndex));
  }

  // イベントのピンを描画
  for (size_t i = 0; i < spawnEvents_.size(); ++i) {
    float evX = p.x + (spawnEvents_[i].spawnTime / maxT) * trackWidth;
    ImVec2 evCenter(evX, p.y + trackHeight / 2.0f);

    // 選択状態なら色を変える
    bool isSelected = (currentSelectType_ == EditorSelectType::SpawnEvent &&
                       selectedSpawnEventIndex_ == static_cast<int>(i));
    ImU32 col =
        isSelected ? IM_COL32(255, 200, 0, 255) : IM_COL32(100, 150, 250, 255);

    // ひし形描画
    float size = 8.0f;
    drawList->AddQuadFilled(ImVec2(evCenter.x, evCenter.y - size),
                            ImVec2(evCenter.x + size, evCenter.y),
                            ImVec2(evCenter.x, evCenter.y + size),
                            ImVec2(evCenter.x - size, evCenter.y), col);

    // クリック判定
    if (isTrackHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      float mouseX = ImGui::GetMousePos().x;
      if (abs(mouseX - evX) < size * 1.5f) {
        currentSelectType_ = EditorSelectType::SpawnEvent;
        selectedSpawnEventIndex_ = static_cast<int>(i);
      }
    }
  }

  // 現在の再生位置(Playhead)を描画
  float playheadX = p.x + (currentT / maxT) * trackWidth;
  drawList->AddLine(ImVec2(playheadX, p.y),
                    ImVec2(playheadX, p.y + trackHeight),
                    IM_COL32(255, 0, 0, 255), 2.0f);

  ImGui::End();

  // クリア後のドラゴンの通った跡・行き先・リザルトの画面の枠
  if (drawToggles_.flightPath) {
    clearSequence_->DrawDebugLines(*engine_->GetLineRenderer());
  }

  // レールのデバッグ描画（スプライン曲線をサンプリングして描画）
  const auto &waypoints = railCamera_->GetWaypoints();
  if (drawToggles_.railLine && waypoints.size() > 1) {
    float maxPathT = static_cast<float>(waypoints.size() - 1);
    const float step = 0.05f; // サンプリング間隔
    Vector3 prevPos = railCamera_->CalcPosition(0.0f);

    // スプライン曲線を滑らかに描画する
    for (float t = step; t <= maxPathT; t += step) {
      Vector3 currentPos = railCamera_->CalcPosition(t);
      engine_->GetLineRenderer()->DrawLine(
          prevPos, currentPos, {0.0f, 1.0f, 1.0f, 1.0f}); // シアン色の線
      prevPos = currentPos;
    }
    // 端数調整（最後の終点まで確実に結ぶ）
    engine_->GetLineRenderer()->DrawLine(
        prevPos, railCamera_->CalcPosition(maxPathT), {0.0f, 1.0f, 1.0f, 1.0f});

    // 現在のカメラ位置に赤い目印をつける（短い線でクロスを描く等）
    Vector3 camPos = railCamera_->CalcPosition(currentT);
    engine_->GetLineRenderer()->DrawLine({camPos.x - 2, camPos.y, camPos.z},
                                         {camPos.x + 2, camPos.y, camPos.z},
                                         {1.0f, 0.0f, 0.0f, 1.0f});
    engine_->GetLineRenderer()->DrawLine({camPos.x, camPos.y - 2, camPos.z},
                                         {camPos.x, camPos.y + 2, camPos.z},
                                         {1.0f, 0.0f, 0.0f, 1.0f});
    engine_->GetLineRenderer()->DrawLine({camPos.x, camPos.y, camPos.z - 2},
                                         {camPos.x, camPos.y, camPos.z + 2},
                                         {1.0f, 0.0f, 0.0f, 1.0f});
  }
#endif
}
void GamePlayScene::DrawEditorUI() {
#ifdef USE_IMGUI
  // ======================================
  // 共通的 Gizmo UI および状態管理
  // ======================================
  static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);
  static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::WORLD);

  // 右クリック中はカメラ操作中なので、Gizmoのショートカットを無効にする
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
    if (ImGui::IsKeyPressed(ImGuiKey_W))
      mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    if (ImGui::IsKeyPressed(ImGuiKey_E))
      mCurrentGizmoOperation = ImGuizmo::ROTATE;
    if (ImGui::IsKeyPressed(ImGuiKey_R))
      mCurrentGizmoOperation = ImGuizmo::SCALE;
  }

  // 共通の描画領域取得 (ImGui::Image
  // の直後に呼ばれる前提なので、直前のItemRectがGame Viewの画像サイズになる)
  ImVec2 vMin = ImGui::GetItemRectMin();
  ImVec2 vMax = ImGui::GetItemRectMax();

  // 選択解除 (Game Viewの画像上で、ギズモ以外を左クリックしたとき)
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (!ImGuizmo::IsOver() && currentSelectType_ != EditorSelectType::None) {
      currentSelectType_ = EditorSelectType::None;
      selectedSpawnEventIndex_ = -1;
      selectedSceneObjectIndex_ = -1;
      selectedWaypointIndex_ = -1;
    }
  }

  // Gizmo用UIを Main Toolbar に追記
  ImGui::Begin("Main Toolbar");

  bool isPlayMode_ = GameManager::GetInstance()->IsGlobalPlayMode();
  if (isPlayMode_) {
    if (isPaused_) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
      if (ImGui::Button(" [ RESUME ] ", ImVec2(80, 0))) {
        isPaused_ = false;
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.7f, 0.2f, 1.0f));
      if (ImGui::Button(" [ STEP ] ", ImVec2(80, 0))) {
        doStep_ = true;
      }
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.7f, 0.2f, 1.0f));
      if (ImGui::Button(" [ PAUSE ] ", ImVec2(80, 0))) {
        isPaused_ = true;
      }
      ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    ImGui::Text(" | ");

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button(" [ KILL BOSS ] ")) {
      // 装甲・形態変化・HP のルールを通さずに倒す
      if (Boss *boss = boss_.Get()) {
        boss->ForceDefeat();
      }
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text(" | ");
  }
  // 当たり判定の線の表示を切り替える
  ImGui::SameLine();
  bool showColliders = CollisionManager::GetInstance()->IsDebugDrawEnabled();
  if (ImGui::Checkbox("Colliders", &showColliders)) {
    CollisionManager::GetInstance()->SetDebugDrawEnabled(showColliders);
  }
  ImGui::SameLine();
  ImGui::Text(" | ");
  ImGui::SameLine();
  ImGui::Text("Gizmo:");
  ImGui::SameLine();

  // Waypoint選択時はTranslateモードに固定する
  bool isWaypointMode = (currentSelectType_ == EditorSelectType::RailCamera &&
                         selectedWaypointIndex_ >= 0);
  if (isWaypointMode) {
    mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGui::TextDisabled("Rotate [E]");
    ImGui::SameLine();
    ImGui::TextDisabled("Scale [R]");
  } else {
    if (ImGui::RadioButton("Translate [W]",
                           mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) {
      mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate [E]",
                           mCurrentGizmoOperation == ImGuizmo::ROTATE)) {
      mCurrentGizmoOperation = ImGuizmo::ROTATE;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale [R]",
                           mCurrentGizmoOperation == ImGuizmo::SCALE)) {
      mCurrentGizmoOperation = ImGuizmo::SCALE;
    }
  }

  if (mCurrentGizmoOperation != ImGuizmo::SCALE) {
    ImGui::SameLine();
    ImGui::Text("  Mode:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL)) {
      mCurrentGizmoMode = ImGuizmo::LOCAL;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD)) {
      mCurrentGizmoMode = ImGuizmo::WORLD;
    }
  }
  // 雲海の描画に GPU でかかった時間（Play 中も見えるようにここに出す）
  ImGui::SameLine();
  ImGui::Text("  Cloud Sea GPU %.2f ms",
              engine_->GetCloudSeaRenderer()->GetGpuMilliseconds());
  ImGui::End();

  //=========================
  // ゲームオーバー演出の調整（Play 中も出す）
  //=========================
  ImGui::Begin("Game Over");
  if (isPlayMode_ && state_->CanEndByDebug() && player_ &&
      ImGui::Button("Kill Player")) {
    player_->Kill();
  }
  gameOverSequence_->DrawImGui();
  if (ImGui::CollapsingHeader("Score")) {
    scoreConfig_.DrawImGui();
    ImGui::Text("Score %d  Kills %d / %d  Worst Section %d",
                scoreKeeper_->GetScore(), scoreKeeper_->GetKilledCount(),
                scoreKeeper_->GetSpawnedCount(),
                scoreKeeper_->GetWorstSection());
  }
  if (ImGui::CollapsingHeader("Skip Hold")) {
    skipHold_->DrawImGui();
  }
  if (player_ && ImGui::CollapsingHeader("Dragon Wing Config")) {
    player_->DrawWingConfigImGui();
  }
  if (player_ && ImGui::CollapsingHeader("Reticle Config")) {
    player_->DrawReticleConfigImGui();
  }
  ImGui::End();

  //=========================
  // クリア演出の調整（Play 中も出す）
  //=========================
  ImGui::Begin("Clear");
  if (isPlayMode_ && state_->CanEndByDebug()) {
    // ボスがいれば、ルールを通さずに倒して最後の一撃から始める
    if (Boss *boss = boss_.Get()) {
      if (!boss->IsDyingOrDefeated() && ImGui::Button("Defeat Boss")) {
        boss->ForceDefeat();
      }
    }
    // ボス戦をせずに、レールカメラの前に仮のボスがいるとして始める
    if (ImGui::Button("Play Clear (No Boss)")) {
      const Vector3 railForward = railCamera_->GetRailForward();
      const Vector3 bossPos =
          railCamera_->GetRailPosition() + railForward * kDebugBossDistance_;
      // ボス戦と同じく、モデルの後ろ（-Z）をカメラへ向ける
      const Vector3 rotation = {
          0.0f,
          std::atan2(-railForward.x, -railForward.z) + std::numbers::pi_v<float>,
          0.0f};
      NotifyBossLastHit({bossPos, kDebugBossSize_, rotation});
    }
  }
  clearSequence_->DrawImGui();
  ImGui::End();

  if (!isPlayMode_) {
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(vMin.x, vMin.y, vMax.x - vMin.x, vMax.y - vMin.y);
  } else {
    return; // PlayMode中はGizmoの操作や描画を行わない
  }

  ICamera *camera = GetActiveCamera();
  if (!camera)
    return;
  Matrix4x4 viewMatrix = camera->GetViewMatrix();
  Matrix4x4 projectionMatrix = camera->GetProjectionMatrix();

  // ======================================
  // SceneObject の Gizmo 編集
  // ======================================
  if (currentSelectType_ == EditorSelectType::SceneObject &&
      selectedSceneObjectIndex_ >= 0 &&
      selectedSceneObjectIndex_ < sceneObjects_.size()) {
    Object3d *selected = sceneObjects_[selectedSceneObjectIndex_].get();
    Vector3 t = selected->GetTranslation();
    Vector3 r = selected->GetRotation();
    Vector3 s = selected->GetScale();

    Matrix4x4 worldMatrix = MakeAffineMatrix(s, r, t);

    if (ImGuizmo::Manipulate(&viewMatrix.m[0][0], &projectionMatrix.m[0][0],
                             mCurrentGizmoOperation, mCurrentGizmoMode,
                             &worldMatrix.m[0][0])) {
      float matrixTranslation[3], matrixRotation[3], matrixScale[3];
      ImGuizmo::DecomposeMatrixToComponents(
          &worldMatrix.m[0][0], matrixTranslation, matrixRotation, matrixScale);
      t = {matrixTranslation[0], matrixTranslation[1], matrixTranslation[2]};

      // マイナススケール対策: スケールを常に正にする
      s = {abs(matrixScale[0]), abs(matrixScale[1]), abs(matrixScale[2])};

      // 角度はそのまま適用
      r = {matrixRotation[0] * (std::numbers::pi_v<float> / 180.0f),
           matrixRotation[1] * (std::numbers::pi_v<float> / 180.0f),
           matrixRotation[2] * (std::numbers::pi_v<float> / 180.0f)};

      selected->SetTranslation(t);
      selected->SetRotation(r);
      selected->SetScale(s);
    }
  }
  // ======================================
  // Waypoint の Gizmo 編集
  // ======================================
  else if (isWaypointMode) {
    if (railCamera_) {
      auto &waypoints = railCamera_->GetWaypointsRef();
      if (selectedWaypointIndex_ < waypoints.size()) {
        Vector3 wp = waypoints[selectedWaypointIndex_];
        Matrix4x4 worldMatrix = MakeTranslateMatrix(wp);

        ImGuizmo::Manipulate(&viewMatrix.m[0][0], &projectionMatrix.m[0][0],
                             mCurrentGizmoOperation, mCurrentGizmoMode,
                             &worldMatrix.m[0][0]);

        if (ImGuizmo::IsUsing()) {
          waypoints[selectedWaypointIndex_] = {
              worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2]};
        }
      }
    }
  }
  // ======================================
  // SpawnEvent の Gizmo 編集 (ゴースト)
  // ======================================
  else if (currentSelectType_ == EditorSelectType::SpawnEvent &&
           selectedSpawnEventIndex_ >= 0 &&
           selectedSpawnEventIndex_ < spawnEvents_.size()) {
    SpawnEvent &ev = spawnEvents_[selectedSpawnEventIndex_];

    if (railCamera_) {
      // 指定時間(ev.spawnTime)における「レールの基準座標と向き」を計算
      Vector3 rawCamPos = railCamera_->CalcPosition(ev.spawnTime);
      Vector3 tangent = railCamera_->CalcTangent(ev.spawnTime);
      Vector3 worldUp = {0.0f, 1.0f, 0.0f};

      Vector3 forward = tangent;
      Vector3 right = SafeNormalize(Cross(worldUp, forward));
      Vector3 up = SafeNormalize(Cross(forward, right));

      Vector3 camPos = rawCamPos;
      camPos.y += 2.5f;

      // そのカメラ位置から、ev.spawnOffset
      // 分だけローカル空間で移動させた位置がゴーストのワールド座標
      Vector3 ghostWorldPos =
          camPos +
          Vector3{right.x * ev.spawnOffset.x, right.y * ev.spawnOffset.x,
                  right.z * ev.spawnOffset.x} +
          Vector3{up.x * ev.spawnOffset.y, up.y * ev.spawnOffset.y,
                  up.z * ev.spawnOffset.y} +
          Vector3{forward.x * ev.spawnOffset.z, forward.y * ev.spawnOffset.z,
                  forward.z * ev.spawnOffset.z};

      // ゴーストのワールド行列
      Matrix4x4 ghostWorldMatrix = MakeTranslateMatrix(ghostWorldPos);

      // Gizmo で動かす (Translate のみ)
      static SpawnEvent s_gizmoStartState;
      static bool s_wasUsingGizmo = false;
      bool isUsingGizmo = ImGuizmo::IsUsing();

      if (isUsingGizmo && !s_wasUsingGizmo) {
        s_gizmoStartState = ev;
      }

      if (ImGuizmo::Manipulate(&viewMatrix.m[0][0], &projectionMatrix.m[0][0],
                               ImGuizmo::TRANSLATE, mCurrentGizmoMode,
                               &ghostWorldMatrix.m[0][0])) {
        // 動かした後の新しいワールド座標
        Vector3 newGhostPos = {ghostWorldMatrix.m[3][0],
                               ghostWorldMatrix.m[3][1],
                               ghostWorldMatrix.m[3][2]};

        // 新しいワールド座標から、カメラのローカル座標(offset)を逆算する
        Vector3 diff = newGhostPos - camPos;
        ev.spawnOffset.x =
            diff.x * right.x + diff.y * right.y + diff.z * right.z;
        ev.spawnOffset.y = diff.x * up.x + diff.y * up.y + diff.z * up.z;
        ev.spawnOffset.z =
            diff.x * forward.x + diff.y * forward.y + diff.z * forward.z;
      }

      if (!isUsingGizmo && s_wasUsingGizmo) {
        CommandManager::GetInstance()->ExecuteCommand(
            std::make_unique<CmdModifySpawnEvent>(
                this, selectedSpawnEventIndex_, s_gizmoStartState, ev));
      }
      s_wasUsingGizmo = isUsingGizmo;

      // 3D空間へのゴーストの描画
      engine_->GetLineRenderer()->DrawLine(ghostWorldPos - Vector3{1, 0, 0},
                                           ghostWorldPos + Vector3{1, 0, 0},
                                           {1, 0, 1, 1});
      engine_->GetLineRenderer()->DrawLine(ghostWorldPos - Vector3{0, 1, 0},
                                           ghostWorldPos + Vector3{0, 1, 0},
                                           {1, 0, 1, 1});
      engine_->GetLineRenderer()->DrawLine(ghostWorldPos - Vector3{0, 0, 1},
                                           ghostWorldPos + Vector3{0, 0, 1},
                                           {1, 0, 1, 1});
    }
  }

  //=========================
  // 開始演出と空の調整
  //=========================
  ImGui::Begin("Stage Intro");
  ImGui::Checkbox("Play Intro On Start", &playIntroOnStart_);
  if (isPlayMode_ && ImGui::Button("Replay Intro")) {
    ChangeState(std::make_unique<IntroState>(*this));
  }
  stageIntro_->DrawImGui();
  roarDistortion_->DrawImGui();
  ImGui::SeparatorText("Lighting");
  stageLighting_->DrawImGui();
  ImGui::SeparatorText("Cloud Sea");
  cloudSeaEvents_->DrawImGui();
  if (player_) {
    stageIntro_->DrawCloudSeaImGui(player_->GetTransform().translate,
                                   railCamera_->GetRailForward());
  }
  ImGui::End();

#endif
}

void GamePlayScene::Draw() { Draw3D(); }

void GamePlayScene::Draw3D() {
  engine_->Begin3D();

  if (skybox_ && drawToggles_.sky) {
    engine_->GetSkyboxRenderer()->Begin();
    skybox_->Draw();

    // 通常3Dへ戻す
    engine_->GetObject3dRenderer()->Begin();
  }

  // 敵の描画
  if (drawToggles_.enemies) {
    for (auto &enemy : runtimeEnemies_) {
      if (!enemy->IsDead()) {
        enemy->Draw3D();
      }
    }
    for (auto &fortress : fortresses_) {
      fortress->Draw();
    }
    for (auto &serpent : serpents_) {
      serpent->Draw();
    }
  }
  if (drawToggles_.sceneObjects) {
    for (auto &obj : sceneObjects_) {
      obj->Draw();
    }
  }

  // クリア演出の光の粒・光の玉・柱・天辺の玉
  if (drawToggles_.clearLight) {
    clearSequence_->Draw3D();
  }

  // プレビューオブジェクトの描画
  if (isPreviewHovering_ && previewObject_) {
    previewObject_->Draw();
  }

  if (player_ && drawToggles_.player) {
    player_->Draw3D();
  }

  // アクター群（弾など）の描画
  if (drawToggles_.actors) {
    ActorManager::GetInstance()->Draw3D();
  }

  // 雲海は不透明な物のあと、パーティクルなど半透明の物の前に描く（深度を読んで物の手前まで重ねる）
  if (skybox_ && drawToggles_.cloudSea) {
    cloudSea_->Draw();
    engine_->GetObject3dRenderer()->Begin();
  }

  // デバッグ用の線を描画
  const ICamera *activeCamera = GetActiveCamera();
  if (activeCamera == nullptr) {
    activeCamera = railCamera_.get();
  }
#ifdef USE_IMGUI
  if (useDebugCamera_) {
    activeCamera = debugCamera_->GetCamera();
  }

  // デバッグ描画
  CollisionManager::GetInstance()->DrawDebug();

  // 選択中の敵レールのデバッグ描画
  if (currentSelectType_ == EditorSelectType::SpawnEvent &&
      selectedSpawnEventIndex_ >= 0 &&
      selectedSpawnEventIndex_ < spawnEvents_.size()) {
    const auto &ev = spawnEvents_[selectedSpawnEventIndex_];
    if (!ev.splineName.empty()) {
      auto it = loadedSplines_.find(ev.splineName);
      if (it != loadedSplines_.end()) {
        const auto &points = it->second;
        if (points.size() >= 2) {
          Vector4 orange = {1.0f, 0.5f, 0.0f, 1.0f};
          Vector3 cameraPos = {0, 0, 0};
          Vector3 cameraRight = {1, 0, 0};
          Vector3 cameraUp = {0, 1, 0};
          Vector3 cameraForward = {0, 0, 1};

          if (!ev.isWorldSpaceSpline && railCamera_) {
            cameraPos = railCamera_->GetRailPosition();
            cameraRight = railCamera_->GetRailRight();
            cameraUp = railCamera_->GetRailUp();
            cameraForward = railCamera_->GetRailForward();
          }

          for (size_t i = 0; i + 1 < points.size(); ++i) {
            Vector3 p0 = points[i];
            Vector3 p1 = points[i + 1];

            if (!ev.isWorldSpaceSpline) {
              Vector3 local0 = {p0.x + ev.spawnOffset.x,
                                p0.y + ev.spawnOffset.y,
                                p0.z + ev.spawnOffset.z};
              Vector3 local1 = {p1.x + ev.spawnOffset.x,
                                p1.y + ev.spawnOffset.y,
                                p1.z + ev.spawnOffset.z};

              p0 = cameraPos +
                   Vector3{cameraRight.x * local0.x, cameraRight.y * local0.x,
                           cameraRight.z * local0.x} +
                   Vector3{cameraUp.x * local0.y, cameraUp.y * local0.y,
                           cameraUp.z * local0.y} +
                   Vector3{cameraForward.x * local0.z,
                           cameraForward.y * local0.z,
                           cameraForward.z * local0.z};

              p1 = cameraPos +
                   Vector3{cameraRight.x * local1.x, cameraRight.y * local1.x,
                           cameraRight.z * local1.x} +
                   Vector3{cameraUp.x * local1.y, cameraUp.y * local1.y,
                           cameraUp.z * local1.y} +
                   Vector3{cameraForward.x * local1.z,
                           cameraForward.y * local1.z,
                           cameraForward.z * local1.z};
            }

            LineRenderer::GetInstance()->DrawLine(p0, p1, orange);
          }
        }
      }
    }
  }

  engine_->GetLineRenderer()->Render(activeCamera->GetViewProjectionMatrix());
#endif

  // パーティクルの更新と発生（ルートシグネチャが変わるため、3Dモデル描画後に）
  ParticleManager::GetInstance()->Update();
  if (activeCamera) {
    stageIntro_->UpdateParticles(activeCamera->GetViewMatrix(),
                                 activeCamera->GetProjectionMatrix());
  }
  ParticleManager::GetInstance()->Emit();

  if (drawToggles_.particles) {
    EffectManager::GetInstance()->Draw();
    stageIntro_->DrawParticles();
  }

  // リボントレイル（弾やミサイルの光の軌跡）の一括描画
  if (activeCamera && drawToggles_.trails) {
    auto srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU(
        "resources/gradationLine.png");
    TrailRenderer::GetInstance()->Render(
        activeCamera->GetViewProjectionMatrix(), srvHandle);
  }

  engine_->End3D();
}

void GamePlayScene::Draw2D() {
  if (!drawToggles_.ui) {
    return;
  }
  // 照準・敵の 2D 描画（ボスの UI など）・迎撃の印（光る節のまわりの放電・自機へ走る電撃）
  if (state_->ShowsAimUI()) {
    if (player_) {
      player_->Draw2D();
    }
    for (auto &enemy : runtimeEnemies_) {
      if (!enemy->IsDead()) {
        enemy->Draw2D();
      }
    }
    interceptCue_->Draw();
  }

  // スプライト（UI）と HUD の描画。出すかは今の状態が決める
  if (state_->ShowsUI()) {
    UIManager::GetInstance()->Draw();
  }
  if (state_->ShowsHud()) {
    hud_->Draw();
  }
  // 今の状態の画面（リザルトなど）
  state_->DrawBehindCards();

  // クリア演出の上下の黒い帯と文字のカード
  clearSequence_->Draw2D();

  // 目標カードは HUD より手前に出す
  stageIntro_->DrawCaption();

  // 今の状態の、カードより手前の画面（ゲームオーバーの黒い幕と「GAME OVER」のカードなど）
  state_->DrawInFrontOfCards();

  // 演出を飛ばす長押しの案内は一番手前に出す
  skipHold_->Draw();

  // ポーズ画面は、止めた画面の上に出す
  pauseMenu_->Draw();
}

void GamePlayScene::StartIntro() {
  skipHold_->Start();

  // レールを止めたまま、操作開始時のレールカメラと自機の位置を決める
  railCamera_->SetAutoMove(false);
  railCamera_->Update();
  player_->ForceSnapToCamera();
  railCamera_->SetPlayerWorldPosition(player_->GetTransform().translate);
  railCamera_->Update();

  stageIntro_->Start(*railCamera_, player_->GetTransform().translate,
                     player_->GetTransform().rotate, cloudSea_->GetHeight());

  // HUD と照準は消しておき、演出の後半で現す
  hudFadeTimer_ = 0.0f;
  UIManager::GetInstance()->SetGlobalAlpha(0.0f);
  player_->SetReticleAlpha(0.0f);
}

void GamePlayScene::UpdateIntro() {
  // 一度最後まで見ていれば、決定の長押しで飛ばせる
  const bool canSkip = GameManager::GetInstance()->HasSeenStageIntro() &&
                       stageIntro_->CanSkip();
  if (skipHold_->Update(engine_->GetInputManager(), canSkip)) {
    stageIntro_->Skip();
  }

  stageIntro_->Update(*railCamera_);

  // レール：演出の後半から 0 → 通常の速さへ上げる
  const float railSpeedRatio = stageIntro_->GetRailSpeedRatio();
  railCamera_->SetSpeedScale(railSpeedRatio);
  if (railSpeedRatio > 0.0f) {
    railCamera_->SetAutoMove(true);
  }

  // 開始の合図：ドラゴンの吠え声
  if (stageIntro_->ConsumeRoarCue()) {
    SoundManager::GetInstance()->PlaySE("dragon_roar", kRoarSeVolume_);
  }

  // 溜め → 吠える → 飛び出しの姿勢
  player_->SetWingGesture(stageIntro_->GetGesture());

  // HUD は声が弱まる頃から、照準は操作が効き始めてから現す
  // （終わったあとは、Play の HUD の表示の続きから現れきる）
  const float hudAlpha = stageIntro_->GetHudAlpha();
  UIManager::GetInstance()->SetGlobalAlpha(hudAlpha);
  player_->SetReticleAlpha(stageIntro_->GetControlRatio());
  hudFadeTimer_ = hudFadeSec_ * hudAlpha;

  if (state_->IsPlayerPosedBySequence()) {
    // 自機の位置・向きを演出側で決める
    Transform &playerTransform = player_->GetTransform();
    playerTransform.translate = stageIntro_->GetDragonPosition();
    playerTransform.rotate = stageIntro_->GetDragonRotation();
    player_->UseExternalTransform();
    player_->SetExternalWingState(stageIntro_->GetWingState());
  }
}

bool GamePlayScene::IsIntroControlStarted() const {
  return stageIntro_->IsControlStarted();
}

void GamePlayScene::FinishIntro() {
  GameManager::GetInstance()->SetSeenStageIntro();
  player_->SetExternalWingState(std::nullopt);
}

void GamePlayScene::BlendPlayerWithIntro() {
  // 操作の効き具合 0 で演出の位置・向き、1 で操作の位置・向き
  const float control = stageIntro_->GetControlRatio();
  Transform &playerTransform = player_->GetTransform();
  playerTransform.translate = Lerp(stageIntro_->GetDragonPosition(),
                                   playerTransform.translate, control);
  // 向きは差を -π〜π に丸めて、近い向きへ混ぜる
  const Vector3 &introRotate = stageIntro_->GetDragonRotation();
  playerTransform.rotate = {LerpAngle(introRotate.x, playerTransform.rotate.x, control),
                            LerpAngle(introRotate.y, playerTransform.rotate.y, control),
                            LerpAngle(introRotate.z, playerTransform.rotate.z, control)};
  player_->BlendOrientationFrom(introRotate, control);
  player_->UpdateTransform();
}

void GamePlayScene::StartGameOver() {
  railCamera_->SetAutoMove(false);
  SoundManager::GetInstance()->StopBGM();
  worldTimeCarry_ = 0.0f;
  skipHold_->Start();
  gameOverSequence_->Start(*railCamera_, player_->GetTransform().translate,
                           player_->GetTransform().rotate,
                           cloudSea_->GetHeight());
  // 翼の付け根の角度は、被弾した瞬間の角度から、演出の一打ちの表で決める
  gameOverSequence_->SetStartWingRootAngles(player_->GetWingRootAngle(false),
                                            player_->GetWingRootAngle(true));
  const GameOverSequence *sequence = gameOverSequence_.get();
  player_->SetDirectWingRoot([sequence](bool isRight, float delaySec) {
    return sequence->WingRootAngle(isRight, delaySec);
  });
}

void GamePlayScene::UpdateGameOver() {
  const bool isSkipPressed = skipHold_->Update(
      engine_->GetInputManager(), gameOverSequence_->CanSkip());
  // 翼の打ち下ろしを渡し、体の浮き・揺れに使う
  gameOverSequence_->SetWingPush(player_->GetWingPush(false),
                                 player_->GetWingPush(true));
  gameOverSequence_->RecordWingAngle(player_->GetWingRootAngle(false),
                                     player_->GetWingRootAngle(true));
  gameOverSequence_->SetFaceAngle(player_->GetFaceAngle());
  gameOverSequence_->Update(isSkipPressed);

  // 自機の位置・向き・翼は演出が決める
  Transform &playerTransform = player_->GetTransform();
  playerTransform.translate = gameOverSequence_->GetDragonPosition();
  playerTransform.rotate = gameOverSequence_->GetDragonRotation();
  player_->UseExternalTransform();
  player_->SetExternalWingState(gameOverSequence_->GetWingState());
  player_->SetWingGesture(gameOverSequence_->GetGesture());

  // HUD は被弾から消していく
  UIManager::GetInstance()->SetGlobalAlpha(gameOverSequence_->GetHudAlpha());

  // 合図で、ボスまでの道のりのどこまで進んだか（ボスが出る位置で 1）を拠点へ渡し、拠点へ切り替える
  // ドラゴンが沈んだ雲海と同じ暗い灰青の雲が、黒い画面を下から覆い、拠点で下から晴れる
  if (gameOverSequence_->ConsumeTransitionCue()) {
    const float bossT = GetBossSpawnTime();
    const float progress =
        bossT > 0.0f ? railCamera_->GetT() / bossT : 1.0f;
    GameManager::GetInstance()->SetGameOverReturn(
        std::clamp(progress, 0.0f, 1.0f));
    const float kDarkCloudTransitionSec = 0.8f;
    DarkCloudTransition::SetNext(kDarkCloudTransitionSec,
                                 Fade::FadeType::CloudDescend);
    SceneManager::GetInstance()->ChangeScene("STAGE_SELECT");
  }
}

void GamePlayScene::StartClear() {
  SoundManager::GetInstance()->StopBGM();
  SoundManager::GetInstance()->PlaySE("victory_jingle");
  if (railCamera_) {
    railCamera_->SetAutoMove(false);
  }
  FinishStageResult();
  resultSequence_->Start();
}

void GamePlayScene::StartClearSequence(const BossLastHit &hit) {
  SoundManager::GetInstance()->StopBGM();
  if (railCamera_) {
    railCamera_->SetAutoMove(false);
  }
  worldTimeCarry_ = 0.0f;
  skipHold_->Start();

  // 点はこの一撃で確定し、記録を書く。リザルトは演出の終わりに出す
  resultSequence_->Stop();
  scoreKeeper_->OnBossKilled();
  FinishStageResult();

  // ボスの撃破の見た目は演出が進める
  if (Boss *boss = boss_.Get()) {
    boss->SetDyingControlled(true);
  }
  clearSequence_->Start(*railCamera_, hit.position, hit.size, hit.rotation,
                        Boss::GetBlastPoints());
}

void GamePlayScene::UpdateClearSequence() {
  const bool isSkipPressed = skipHold_->Update(engine_->GetInputManager(),
                                               clearSequence_->CanSkip());
  clearSequence_->Update(isSkipPressed);

  // ボス：爆発のたびに向きが崩れ、赤く光る（場所は動かさない）
  Boss *boss = boss_.Get();
  if (boss && !boss->IsDead()) {
    boss->SetDyingOrientation(clearSequence_->GetBossOrientation());
    boss->SetDyingFlash(clearSequence_->GetBossFlashColor(),
                        clearSequence_->GetBossFlash());
  }

  for (ClearSequence::Cue cue : clearSequence_->TakeCues()) {
    switch (cue) {
    case ClearSequence::Cue::BossBlast:
      // 大爆発：体を割った破片にして飛び散らせ、ボスを消す
      if (boss && !boss->IsDead()) {
        if (clearSequence_->IsBlastVisible()) {
          SpawnBossDebris(*boss);
          SoundManager::GetInstance()->PlaySE("boss_explosion");
        }
        boss->FinishDying();
      }
      break;
    case ClearSequence::Cue::Burst:
      SoundManager::GetInstance()->PlaySE("victory_jingle");
      break;
    case ClearSequence::Cue::Result:
      resultSequence_->Start();
      break;
    }
  }

  // 自機：余韻の場面から、演出の経路で晴れた空を飛ぶ
  if (clearSequence_->IsDragonFlying()) {
    Transform &playerTransform = player_->GetTransform();
    playerTransform.translate = clearSequence_->GetDragonPosition();
    playerTransform.rotate = clearSequence_->GetDragonRotation();
    player_->UseExternalOrientation(clearSequence_->GetDragonOrientation());
    player_->SetExternalWingState(clearSequence_->GetWingState());
    // 太陽を背にしたときの縁の光
    player_->SetBackRimScale(clearSequence_->GetDragonBackRimScale());
  }

  // HUD と照準は最後の一撃から消していく
  const float hudAlpha = clearSequence_->GetHudAlpha();
  UIManager::GetInstance()->SetGlobalAlpha(hudAlpha);
  player_->SetReticleAlpha(hudAlpha);
}

void GamePlayScene::ChangeState(std::unique_ptr<GamePlayState> next) {
  state_ = std::move(next);
  state_->Enter();
}

void GamePlayScene::NotifyBossLastHit(const BossLastHit &hit) {
  if (std::unique_ptr<GamePlayState> next = state_->OnBossLastHit(hit)) {
    ChangeState(std::move(next));
  }
}

bool GamePlayScene::IsPlayerDead() const { return player_ && player_->IsDead(); }

bool GamePlayScene::IsRailFinished() const {
  return railCamera_ && railCamera_->IsFinished();
}

bool GamePlayScene::IsBossAlive() const { return boss_.Get() != nullptr; }

void GamePlayScene::UpdateHudFadeIn() {
  if (hudFadeTimer_ >= hudFadeSec_) {
    return;
  }
  const float kFrameSec = 1.0f / 60.0f;
  hudFadeTimer_ = (std::min)(hudFadeTimer_ + kFrameSec, hudFadeSec_);
  const float hudAlpha = hudFadeTimer_ / hudFadeSec_;
  UIManager::GetInstance()->SetGlobalAlpha(hudAlpha);
  if (player_) {
    player_->SetReticleAlpha(hudAlpha);
  }
}

float GamePlayScene::GetIntroRailSpeedRatio() const {
  return stageIntro_->GetRailSpeedRatio();
}

float GamePlayScene::GetGameOverTimeScale() const {
  return gameOverSequence_->GetTimeScale();
}

float GamePlayScene::GetClearTimeScale() const {
  return clearSequence_->IsActive() ? clearSequence_->GetTimeScale() : 1.0f;
}

const ICamera *GamePlayScene::UpdateGameOverScreen(bool canMove) {
  if (!gameOverSequence_->IsActive()) {
    return nullptr;
  }
  if (canMove) {
    UpdateGameOver();
  }
  return gameOverSequence_->GetCamera();
}

const ICamera *GamePlayScene::UpdateClearScreen(bool canMove) {
  if (!clearSequence_->IsActive()) {
    return nullptr;
  }
  if (canMove) {
    UpdateClearSequence();
  }
  return clearSequence_->GetCamera();
}

float GamePlayScene::GetGameOverHudAlpha() const {
  return gameOverSequence_->GetHudAlpha();
}

float GamePlayScene::GetClearHudAlpha() const {
  return clearSequence_->GetHudAlpha();
}

bool GamePlayScene::IsResultActive() const { return resultSequence_->IsActive(); }

void GamePlayScene::DrawResult() { resultSequence_->Draw(); }

void GamePlayScene::DrawGameOverScreen() { gameOverSequence_->Draw(); }

void GamePlayScene::SpawnBossDebris(const Boss &boss) {
  Object3d *model = boss.GetModel();
  Object3dRenderer *renderer = PrefabManager::GetInstance()->GetObject3dRenderer();
  if (!model || !renderer) {
    return;
  }
  const FracturePieceSet *pieceSet =
      FractureCache::GetInstance()->Find(model->GetModel());
  if (!pieceSet) {
    return;
  }
  // 破片は爆発の瞬間のボスの位置・向き・大きさで置く（向きは崩れたあとのモデルの回転）
  const Transform &transform = boss.GetTransform();
  // 崩れたあとの向きを、MakeRotateMatrix（X → Y → Z の順に回す）のオイラー角に直す
  const Vector3 kUnitScale = {1.0f, 1.0f, 1.0f};
  const Vector3 kNoTranslate = {0.0f, 0.0f, 0.0f};
  const Matrix4x4 rotateMatrix = MakeAffineMatrix(
      kUnitScale, clearSequence_->GetBossOrientation(), kNoTranslate);
  const Vector3 rotation = {
      std::atan2(rotateMatrix.m[1][2], rotateMatrix.m[2][2]),
      std::asin(std::clamp(-rotateMatrix.m[0][2], -1.0f, 1.0f)),
      std::atan2(rotateMatrix.m[0][1], rotateMatrix.m[0][0])};
  const Vector3 kNoInheritedVelocity = {0.0f, 0.0f, 0.0f};
  auto debris = std::make_unique<FractureDebris>(
      renderer, *pieceSet, transform.translate, rotation, transform.scale,
      clearSequence_->GetDebrisLook(boss.GetBaseColor()), kNoInheritedVelocity,
      clearSequence_->GetDebrisParams());
  ActorManager::GetInstance()->AddActor(std::move(debris));
}

void GamePlayScene::FinishStageResult() {
  // まだ弾が飛んでいる射撃を確定し、ランクを決める
  scoreKeeper_->FinishAllVolleys();
  ResultSequence::Result result;
  result.breakdown = scoreKeeper_->GetBreakdown();
  result.spawned = scoreKeeper_->GetSpawnedCount();
  result.scoreRank = scoreConfig_.GetScoreRank(scoreKeeper_->GetScore());
  result.killRateRank =
      scoreConfig_.GetKillRateRank(scoreKeeper_->GetKillRate());
  result.overallRank =
      ScoreConfig::GetOverallRank(result.scoreRank, result.killRateRank);
  result.hintSection = scoreKeeper_->GetWorstSection();

  // 記録を書く（ベストはスコア・ランク・撃墜率それぞれの最高）
  const std::string stageName = GameManager::GetInstance()->GetCurrentLevel();
  StageRecord record = StageRecord::Load(stageName);
  result.hasPreviousBest = record.hasRecord;
  result.previousBestScore = record.bestScore;
  result.isNewBest =
      !record.hasRecord || scoreKeeper_->GetScore() > record.bestScore;
  record.bestScore = (std::max)(record.bestScore, scoreKeeper_->GetScore());
  record.bestRank = record.hasRecord
                        ? (std::max)(record.bestRank, result.overallRank)
                        : result.overallRank;
  record.bestKillRate =
      (std::max)(record.bestKillRate, scoreKeeper_->GetKillRate());
  record.hasRecord = true;
  StageRecord::Save(stageName, record);
  result.bestScore = record.bestScore;

  resultSequence_->Prepare(result);
}

void GamePlayScene::UpdateClear() {
  // リザルトはクリア演出の終わりに出る
  if (!resultSequence_->IsActive()) {
    return;
  }
  Input *input = GameManager::GetInstance()->IsGlobalPlayMode()
                     ? engine_->GetInputManager()
                     : nullptr;
  resultSequence_->Update(input);

  switch (resultSequence_->ConsumeChoice()) {
  case ResultSequence::Choice::Again:
    SoundManager::GetInstance()->PlaySE("ui_decide");
    RetryStage();
    break;
  case ResultSequence::Choice::Base:
    SoundManager::GetInstance()->PlaySE("ui_decide");
    ReturnToBase();
    break;
  case ResultSequence::Choice::None:
    break;
  }
}

bool GamePlayScene::UpdatePauseMenu(Input *input) {
  // このフレームの初めに開いていたか（閉じたフレームの決定の押下を、自機の射撃に渡さない）
  const bool wasOpen = pauseMenu_->IsOpen();
  if (!wasOpen) {
    if (input && state_->CanPause() && !isPaused_ &&
        MenuInput::IsPauseTriggered(input)) {
      pauseMenu_->Open();
      SoundManager::GetInstance()->StopAllSE();
      SoundManager::GetInstance()->SetBGMVolume(kPauseBgmVolume_);
    }
    return pauseMenu_->IsOpen();
  }

  // 決定の音は板が鳴らす
  pauseMenu_->Update(input);
  switch (pauseMenu_->ConsumeChoice()) {
  case PauseMenu::Choice::Resume:
    SoundManager::GetInstance()->SetBGMVolume(1.0f);
    break;
  case PauseMenu::Choice::Again:
    RetryStage();
    break;
  case PauseMenu::Choice::Base:
    ReturnToBase();
    break;
  case PauseMenu::Choice::None:
    break;
  }
  return true;
}

void GamePlayScene::RetryStage() {
  // 拠点へ降りずに、雲を抜けてもう一度ステージの開始演出から
  SceneManager::GetInstance()->SetNextTransitionFade(
      kCloudTransitionSec_, Fade::FadeType::Cloud, kCloudTransitionColor_,
      Fade::EasingType::Linear);
  SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
}

void GamePlayScene::ReturnToBase() {
  // ステージセレクト（雲の下の拠点）へ戻るので、雲を抜けて降下する
  SceneManager::GetInstance()->SetNextTransitionFade(
      kCloudTransitionSec_, Fade::FadeType::CloudDescend, kCloudTransitionColor_,
      Fade::EasingType::Linear);
  SceneManager::GetInstance()->ChangeScene("STAGE_SELECT");
}

float GamePlayScene::GetBossSpawnTime() const {
  const size_t kMinWaypointCount = 2;
  float bossT = static_cast<float>(
      (std::max)(railCamera_->GetWaypoints().size(), kMinWaypointCount) - 1);
  for (const auto &ev : spawnEvents_) {
    if (ev.prefabName == "Boss") {
      bossT = ev.spawnTime;
      break;
    }
  }
  return bossT;
}

void GamePlayScene::ApplySkyAndLighting() {
  // クリア演出の晴れの進み具合を、空・光・霧と雲海へ渡す
  if (clearSequence_) {
    stageLighting_->SetClearState(clearSequence_->GetClearState());
  }
  stageLighting_->Apply(skybox_.get(), engine_->GetObject3dRenderer(),
                        postProcess_.get(), skyTimer_);
  // 雲海は空と同じ晴れの円で、視線の向きごとに晴れた色へ変わる
  CloudSeaClearing clearing;
  stageLighting_->FillCloudSeaClearing(clearing, skyTimer_);
  cloudSeaEvents_->SetClearAmount(stageLighting_->GetClearPreview());
  cloudSeaEvents_->Apply(cloudSea_.get(), clearing);
}

void GamePlayScene::SaveLevel(const std::string &filename) {
  nlohmann::json root;

  nlohmann::json spawnEventsArray = nlohmann::json::array();
  for (auto &ev : spawnEvents_) {
    nlohmann::json evJson;
    evJson["spawnTime"] = ev.spawnTime;
    evJson["prefabName"] = ev.prefabName;
    evJson["spawnOffset"] = {ev.spawnOffset.x, ev.spawnOffset.y,
                             ev.spawnOffset.z};
    evJson["splineName"] = ev.splineName;
    evJson["splineDuration"] = ev.splineDuration;
    evJson["isWorldSpace"] = ev.isWorldSpace;
    evJson["isWorldSpaceSpline"] = ev.isWorldSpaceSpline;
    evJson["fireInterval"] = ev.fireInterval;
    evJson["moveType"] = static_cast<int>(ev.moveType);
    evJson["moveDirection"] = {ev.moveDirection.x, ev.moveDirection.y,
                               ev.moveDirection.z};
    if (ev.prefabName == kSerpentPrefabName_) {
      const SerpentSpec &s = ev.serpent;
      nlohmann::json serpentJson;
      serpentJson["cores"] = s.coreSegments;
      serpentJson["stopSeconds"] = s.stopSeconds;
      serpentJson["chargeSeconds"] = s.chargeSeconds;
      serpentJson["appearSeconds"] = s.appearSeconds;
      if (s.zapMax > 0) {
        serpentJson["zapMax"] = s.zapMax;
      }
      if (s.wallZap) {
        serpentJson["wallZap"] = true;
      }
      if (s.isFinal) {
        serpentJson["final"] = true;
      }
      if (s.warnSeconds > 0.0f) {
        serpentJson["warnSeconds"] = s.warnSeconds;
        serpentJson["warnPoint"] = {s.warnPoint.x, s.warnPoint.y, s.warnPoint.z};
        serpentJson["warnYaw"] = s.warnYaw;
      }
      evJson["serpent"] = serpentJson;
    }
    spawnEventsArray.push_back(evJson);
  }
  root["spawnEvents"] = spawnEventsArray;

  nlohmann::json sceneObjectsArray = nlohmann::json::array();
  for (auto &obj : sceneObjects_) {
    nlohmann::json oJson;
    Vector3 t = obj->GetTranslation();
    Vector3 r = obj->GetRotation();
    Vector3 s = obj->GetScale();
    oJson["translation"] = {t.x, t.y, t.z};
    oJson["rotation"] = {r.x, r.y, r.z};
    oJson["scale"] = {s.x, s.y, s.z};
    oJson["modelPath"] = obj->GetModelPath();
    const Vector4 color = obj->GetColor();
    oJson["color"] = {color.x, color.y, color.z};
    oJson["specularScale"] = obj->GetSpecularScale();
    // テクスチャの繰り返し回数（1 のときは書かない）
    const Matrix4x4 uv = obj->GetUVTransform();
    if (uv.m[0][0] != 1.0f || uv.m[1][1] != 1.0f) {
      oJson["uvScale"] = {uv.m[0][0], uv.m[1][1]};
    }
    sceneObjectsArray.push_back(oJson);
  }
  root["sceneObjects"] = sceneObjectsArray;

  // 保存先フォルダ（resources/levels）が存在しない場合は作成する
  std::filesystem::create_directories("resources/levels");

  // 救の配置データを level_editor.json に保存
  std::string filePath = "resources/levels/" + filename;
  std::ofstream file(filePath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }

  // カメラレールは camera_rail.json に別途保存
  SaveCameraRail();
}

#ifdef USE_IMGUI
void GamePlayScene::DrawLookKeysUI() {
  if (!railCamera_) {
    return;
  }
  railCamera_->DrawLookKeysImGui();
  ImGui::SameLine();
  if (ImGui::Button("カメラの向きを保存")) {
    SaveCameraRail();
  }
  ImGui::Spacing();
}
#endif

void GamePlayScene::SaveCameraRail() {
  nlohmann::json railRoot;
  nlohmann::json waypointsArray = nlohmann::json::array();
  nlohmann::json lookKeysArray = nlohmann::json::array();
  if (railCamera_) {
    for (const auto &wp : railCamera_->GetWaypoints()) {
      waypointsArray.push_back({wp.x, wp.y, wp.z});
    }
    for (const auto &key : railCamera_->GetLookKeys()) {
      lookKeysArray.push_back(
          {{"t", key.t}, {"yaw", key.yaw}, {"roll", key.roll}, {"out", key.out}});
    }
  }
  railRoot["waypoints"] = waypointsArray;
  railRoot["lookKeys"] = lookKeysArray;
  std::filesystem::create_directories("resources/levels");
  std::ofstream railFile("resources/levels/camera_rail.json");
  if (railFile.is_open()) {
    railFile << std::setw(4) << railRoot << std::endl;
  }
}

void GamePlayScene::LoadLevel(const std::string &filename) {
  std::string filePath = "resources/levels/" + filename;
  std::ifstream file(filePath);
  if (!file.is_open())
    return;

  nlohmann::json root;

  // レベルロード時は配列のインデックスや参照が完全に破壊されるため、Undo履歴をリセットする
  CommandManager::GetInstance()->Clear();

  try {
    file >> root;
  } catch (const nlohmann::json::parse_error &e) {
    Logger::Log(std::string("[GamePlayScene] LoadLevel JSON parse error: ") +
                e.what());
    return;
  }

  // 古い敵をクリア
  ClearRuntimeEnemies();
  sceneObjects_.clear();
  selectedSceneObjectIndex_ = -1;
  spawnEvents_.clear();
  selectedSpawnEventIndex_ = -1;

  // 古いセーブデータの互換性維持（enemiesキーがあっても無視する）

  if (root.contains("spawnEvents")) {
    for (auto &evJson : root["spawnEvents"]) {
      SpawnEvent ev;
      ev.spawnTime = evJson["spawnTime"];
      ev.prefabName = evJson["prefabName"];
      if (evJson.contains("spawnOffset")) {
        ev.spawnOffset = {evJson["spawnOffset"][0], evJson["spawnOffset"][1],
                          evJson["spawnOffset"][2]};
      } else if (evJson.contains("spawnPosition")) { // 古いセーブデータ互換
        ev.spawnOffset = {0.0f, 0.0f, 50.0f};
      }
      if (evJson.contains("moveType")) {
        ev.moveType = static_cast<MoveType>(evJson["moveType"].get<int>());
      }
      if (evJson.contains("splineName")) {
        ev.splineName = evJson["splineName"];
      }
      if (evJson.contains("splineDuration")) {
        ev.splineDuration = evJson["splineDuration"];
      }
      if (evJson.contains("isWorldSpace")) {
        ev.isWorldSpace = evJson["isWorldSpace"];
      }
      if (evJson.contains("isWorldSpaceSpline")) {
        ev.isWorldSpaceSpline = evJson["isWorldSpaceSpline"];
      }
      if (evJson.contains("fireInterval")) {
        ev.fireInterval = evJson["fireInterval"];
      }
      if (evJson.contains("moveDirection")) {
        ev.moveDirection = {evJson["moveDirection"][0],
                            evJson["moveDirection"][1],
                            evJson["moveDirection"][2]};
      } else {
        ev.moveDirection = {0.0f, 0.0f, 1.0f}; // 移動方向の指定がなければ前進
      }
      if (evJson.contains("serpent")) {
        const auto &serpentJson = evJson["serpent"];
        ev.serpent.coreSegments = serpentJson.value("cores", std::vector<int>{});
        ev.serpent.stopSeconds = serpentJson.value("stopSeconds", 0.0f);
        ev.serpent.chargeSeconds = serpentJson.value("chargeSeconds", 0.0f);
        ev.serpent.appearSeconds = serpentJson.value("appearSeconds", 0.0f);
        ev.serpent.zapMax = serpentJson.value("zapMax", 0);
        ev.serpent.wallZap = serpentJson.value("wallZap", false);
        ev.serpent.isFinal = serpentJson.value("final", false);
        ev.serpent.warnSeconds = serpentJson.value("warnSeconds", 0.0f);
        if (serpentJson.contains("warnPoint")) {
          ev.serpent.warnPoint = {serpentJson["warnPoint"][0], serpentJson["warnPoint"][1],
                                  serpentJson["warnPoint"][2]};
        }
        ev.serpent.warnYaw = serpentJson.value("warnYaw", 0.0f);
      }

      ev.hasSpawned = false;
      spawnEvents_.push_back(ev);
    }
  }

  if (root.contains("sceneObjects")) {
    for (auto &oJson : root["sceneObjects"]) {
      std::string path = oJson["modelPath"];
      Vector3 t = {oJson["translation"][0], oJson["translation"][1],
                   oJson["translation"][2]};
      Vector3 r = {oJson["rotation"][0], oJson["rotation"][1],
                   oJson["rotation"][2]};
      Vector3 s = {oJson["scale"][0], oJson["scale"][1], oJson["scale"][2]};
      SpawnSceneObject(path, t);
      auto &obj = sceneObjects_.back();
      obj->SetRotation(r);
      obj->SetScale(s);
      // 色と鏡面反射の倍率（ないときは Object3d の既定値のまま）
      if (oJson.contains("color")) {
        obj->SetColor({oJson["color"][0], oJson["color"][1],
                       oJson["color"][2], 1.0f});
      }
      if (oJson.contains("specularScale"))
        obj->SetSpecularScale(oJson["specularScale"]);
      // テクスチャの繰り返し回数（横, 縦）
      if (oJson.contains("uvScale")) {
        obj->SetUVTransform(MakeScaleMatrix(
            {oJson["uvScale"][0].get<float>(), oJson["uvScale"][1].get<float>(), 1.0f}));
      }
    }
  }

  // Waypoint の読み込み（camera_rail.json から別途読む）
  std::ifstream railFile("resources/levels/camera_rail.json");
  if (railFile.is_open()) {
    nlohmann::json railRoot;
    try {
      railFile >> railRoot;
      if (railRoot.contains("waypoints")) {
        std::vector<Vector3> loadedWaypoints;
        for (auto &wJson : railRoot["waypoints"]) {
          loadedWaypoints.push_back({wJson[0], wJson[1], wJson[2]});
        }
        if (railCamera_ && !loadedWaypoints.empty()) {
          railCamera_->Initialize(loadedWaypoints);
        }
      }
      // 顔の向きのキー（ないときは振らない）
      std::vector<RailCamera::LookKey> lookKeys;
      if (railRoot.contains("lookKeys")) {
        for (auto &kJson : railRoot["lookKeys"]) {
          RailCamera::LookKey key;
          key.t = kJson.value("t", 0.0f);
          key.yaw = kJson.value("yaw", 0.0f);
          key.roll = kJson.value("roll", 0.0f);
          key.out = kJson.value("out", 0.0f);
          lookKeys.push_back(key);
        }
      }
      if (railCamera_) {
        railCamera_->SetLookKeys(lookKeys);
      }
    } catch (const nlohmann::json::parse_error &e) {
      Logger::Log(
          std::string("[GamePlayScene] camera_rail.json parse error: ") +
          e.what());
    }
  }
}

void GamePlayScene::CountEnemyForScore(Enemy *enemy, float spawnTime) {
  const int scoreId = scoreKeeper_->OnEnemySpawned(spawnTime);
  enemy->SetOnDestroyedCallback([this, scoreId, enemy](bool isSelfDestruct) {
    if (!isSelfDestruct) {
      scoreKeeper_->OnEnemyKilled(scoreId, enemy->GetLastHitVolley(),
                                  enemy->GetTransform().translate);
    }
  });
}

EnemySpawner &GamePlayScene::SelectSpawner(const std::string &prefabName) {
  if (prefabName == kBossPrefabName_) {
    return *bossSpawner_;
  }
  if (prefabName == kFortressPrefabName_) {
    return *fortressSpawner_;
  }
  if (prefabName == kSerpentPrefabName_) {
    return *serpentSpawner_;
  }
  return *zakoSpawner_;
}

SpawnContext GamePlayScene::MakeSpawnContext() {
  SpawnContext context;
  context.camera = railCamera_.get();
  context.player = player_.get();
  context.object3dRenderer = engine_->GetObject3dRenderer();
  context.spriteRenderer = engine_->GetSpriteRenderer();
  context.splines = &loadedSplines_;
  context.interceptCue = interceptCue_.get();
  context.countForScore = [this](Enemy *enemy, float spawnTime) {
    CountEnemyForScore(enemy, spawnTime);
  };
  return context;
}

void GamePlayScene::AddSpawned(SpawnResult result) {
  for (auto &enemy : result.enemies) {
    runtimeEnemies_.push_back(std::move(enemy));
  }
  if (result.fortress) {
    fortresses_.push_back(std::move(result.fortress));
  }
  if (result.serpent) {
    serpents_.push_back(std::move(result.serpent));
  }
  if (result.boss) {
    OnBossSpawned(result.boss);
  }
}

void GamePlayScene::OnBossSpawned(Boss *boss) {
  boss_ = ActorRef<Boss>(boss);

  // 最後の一撃でクリア演出を始める
  boss->SetOnDyingStartedCallback([this](const Vector3 &pos) {
    const Boss *dyingBoss = boss_.Get();
    const float size =
        dyingBoss ? dyingBoss->GetTransform().scale.x : kDebugBossSize_;
    const Vector3 rotation =
        dyingBoss ? dyingBoss->GetTransform().rotate : Vector3{};
    NotifyBossLastHit({pos, size, rotation});
  });

  // ボス戦開始: レールカメラを低速化し、ボスBGMへ切り替え
  SoundManager::GetInstance()->PlayBGM("boss_bgm");
  if (railCamera_) {
    railCamera_->SetSpeed(0.05f);
  }
}

void GamePlayScene::ClearRuntimeEnemies() {
  runtimeEnemies_.clear();
  fortresses_.clear();
  serpents_.clear();
  if (interceptCue_) {
    interceptCue_->Clear();
  }
  boss_.Reset();
  if (bossCamera_) {
    bossCamera_->Reset();
  }
  if (railCamera_) {
    railCamera_->ResetEventControls();
  }
}

void GamePlayScene::SpawnSceneObject(const std::string &modelPath,
                                     const Vector3 &position) {
  auto obj = std::make_unique<Object3d>();
  obj->Initialize(engine_->GetObject3dRenderer());
  obj->SetModel(modelPath);
  obj->SetTranslation(position);
  sceneObjects_.push_back(std::move(obj));
}

// TransformCoordヘルパー関数を定義
static Vector3 TransformCoord(const Vector3 &v, const Matrix4x4 &m) {
  float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
  Vector3 result{
      (v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0]) / w,
      (v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1]) / w,
      (v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2]) / w};
  return result;
}

static Vector3 CalculateDropPosition(ICamera *currentCamera,
                                     const Vector2 &ndcPos) {
  Vector3 spawnPos = {0.0f, 0.0f, 0.0f};
  if (!currentCamera)
    return spawnPos;

  Matrix4x4 vpMatrix = currentCamera->GetViewProjectionMatrix();
  Matrix4x4 invVP = Inverse(vpMatrix);

  Vector3 ndcNear = {ndcPos.x, ndcPos.y, 0.0f};
  Vector3 ndcFar = {ndcPos.x, ndcPos.y, 1.0f};

  Vector3 worldNear = TransformCoord(ndcNear, invVP);
  Vector3 worldFar = TransformCoord(ndcFar, invVP);

  Vector3 rayDir = Normalize(worldFar - worldNear);
  Vector3 rayOrigin = worldNear;

  // エディタカメラの注視点（Pivot）の代用として、
  // 常にカメラの前方（一定距離）の空間に配置する。
  float defaultDepth = 50.0f;

  // レイの方向に一定距離進んだ位置をスポーン座標とする
  spawnPos = rayOrigin + rayDir * defaultDepth;

  return spawnPos;
}

void GamePlayScene::OnFileDropped(const std::string &filePath,
                                  const Vector2 &ndcPos) {
  ICamera *currentCamera = GetActiveCamera();
  if (currentCamera == nullptr) {
    currentCamera = railCamera_.get();
  }

  Vector3 spawnPos = CalculateDropPosition(currentCamera, ndcPos);
  SpawnSceneObject(filePath, spawnPos);

  currentSelectType_ = EditorSelectType::SceneObject;
  selectedSceneObjectIndex_ = static_cast<int>(sceneObjects_.size() - 1);

  OnDragHoverEnd(); // ドロップ完了時にプレビューを消す
}

void GamePlayScene::OnDragHovering(const std::string &filePath,
                                   const Vector2 &ndcPos) {
  ICamera *currentCamera = GetActiveCamera();
  if (currentCamera == nullptr) {
    currentCamera = railCamera_.get();
  }

  Vector3 spawnPos = CalculateDropPosition(currentCamera, ndcPos);

  if (previewModelPath_ != filePath || !previewObject_) {
    previewObject_ = std::make_unique<Object3d>();
    previewObject_->Initialize(engine_->GetObject3dRenderer());
    previewObject_->SetModel(filePath);
    previewModelPath_ = filePath;
  }

  previewObject_->SetTranslation(spawnPos);
  previewObject_
      ->Update(); // ワールド行列の更新（これがないと原点に描画されてしまう）

  isPreviewHovering_ = true;
}

void GamePlayScene::OnDragHoverEnd() {
  if (previewObject_) {
    previewObject_.reset();
    previewModelPath_ = "";
  }
  isPreviewHovering_ = false;
}
void GamePlayScene::LoadSplines() {
  loadedSplines_.clear();
  std::ifstream splineFile("resources/levels/splines.json");
  if (splineFile.is_open()) {
    nlohmann::json root;
    try {
      splineFile >> root;
      if (root.contains("rails") && root["rails"].is_array()) {
        for (const auto &rail : root["rails"]) {
          std::string name = rail["name"];
          std::vector<Vector3> pts;
          for (const auto &p : rail["points"]) {
            // floatへの変換を安全に行うため get<double>() などで受けてキャスト
            pts.push_back({static_cast<float>(p[0].get<double>()),
                           static_cast<float>(p[1].get<double>()),
                           static_cast<float>(p[2].get<double>())});
          }
          loadedSplines_[name] = pts;
        }
        Logger::Log("Successfully loaded " +
                    std::to_string(loadedSplines_.size()) +
                    " splines from splines.json\n");
      }
    } catch (const std::exception &e) {
      Logger::Log(std::string("Failed to parse splines.json: ") + e.what() +
                  "\n");
    }
  } else {
    Logger::Log("Failed to open resources/levels/splines.json\n");
  }
}
