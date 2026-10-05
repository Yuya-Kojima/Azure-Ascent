#include "GamePlayScene.h"
#include "../../externals/nlohmann/json.hpp"
#include "../Effect/EffectManager.h"
#include "Actor/Behavior/BehaviorSpline.h"
#include "Actor/Behavior/BehaviorStraight.h"
#include "Actor/Behavior/BehaviorStationary.h"
#include "Actor/Behavior/BehaviorSineWave.h"
#include "Actor/Behavior/BehaviorFighter.h"
#include "Actor/Behavior/BehaviorMeteor.h"
#include "Actor/Behavior/BehaviorStrafe.h"
#include "Actor/Behavior/BehaviorTurret.h"
#include "Audio/SoundManager.h"
#include "Camera/GameCamera.h"
#include "Debug/DebugCamera.h"
#include "Debug/ImGuiManager.h"
#include "Debug/Logger.h"
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
#include "Renderer/TrailRenderer.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/SpriteRenderer.h"
#include "Scene/SceneManager.h"
#include "Sprite/Sprite.h"
#include "Texture/TextureManager.h"
#include <filesystem>
#include <fstream>

#include "../Editor/CommandManager.h"
#include "Collision/CollisionManager.h"
#include "Framework/ActorManager.h"
#include "Framework/GameManager.h"
#include "Framework/PrefabManager.h"
#include "Renderer/PostProcess.h"

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

void GamePlayScene::Initialize(EngineBase *engine) {

  // 基底クラスの初期化 (PostProcessの初期化など)
  BaseScene::Initialize(engine);

  // シーン初期化時に前シーンの残留アクター（弾や古いプレイヤー）を全消去
  ActorManager::GetInstance()->Clear();
  CollisionManager::GetInstance()
      ->Clear(); // コライダーの残留（ダングリングポインタ）を防ぐ
  PrefabManager::GetInstance()->Initialize(engine->GetObject3dRenderer());

  // 参照をコピー
  engine_ = engine;

  // --- フォグの初期化 ---
  FogData fog;
  fog.color = Vector4(0.8f, 0.9f, 1.0f, 1.0f); // 空色っぽいフォグ
  fog.nearDist = 300.0f;
  fog.farDist = 600.0f;
  fog.enabled = 1.0f;
  engine_->GetObject3dRenderer()->SetFog(fog);

  // ディレクショナルライト設定
  if (auto *dl = engine->GetObject3dRenderer()->GetDirectionalLightData()) {
    dl->color = {1.0f, 1.0f, 1.0f, 1.0f};
    dl->direction = Normalize(Vector3{0.0f, -1.0f, 0.0f});
    dl->intensity = 1.0f;
  }

  //===========================
  // テクスチャファイルの読み込み
  //===========================

  //===========================
  // オーディオファイルの読み込み
  //===========================
  SoundManager::GetInstance()->Load("boss_explosion",
                                    "resources/Sounds/explosion.mp3");
  SoundManager::GetInstance()->Load("laser_shot",
                                    "resources/Sounds/laser_shot.wav");
  SoundManager::GetInstance()->Load("lockon",
                                    "resources/Sounds/lockon.wav");
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
  SoundManager::GetInstance()->PlayBGM("stage_bgm");

  EffectManager::GetInstance()->Initialize();

  hasBossStartedDying_ = false;

  //===========================
  // スプライト関係の初期化
  //===========================

  //===========================
  // 3Dオブジェクト関係の初期化
  //===========================

  // ===== ボス専用パーティクル初期化 =====
  bossExplosionParticleGroup_ = std::make_unique<BillboardParticleEmitter>();
  bossExplosionParticleGroup_->Initialize("resources/circle.png");
  bossExplosionEmitter_ = std::make_unique<ParticleEmitter>(
      bossExplosionParticleGroup_.get(), Vector3{0, 0, 0},
      Vector3{4.0f, 4.0f, 4.0f}, 100, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
      Vector3{40.0f, 40.0f, 40.0f}, 0.6f, 1.2f);
  bossExplosionEmitter_->SetBaseScale({8.0f, 8.0f, 8.0f});
  bossExplosionEmitter_->SetScaleRandom({3.0f, 3.0f, 3.0f});
  bossExplosionEmitter_->SetScaleVelocity({-5.0f, -5.0f, -5.0f});
  bossExplosionEmitter_->SetColor({0.5f, 2.0f, 2.5f, 1.0f});

  bossDustParticleGroup_ = std::make_unique<BillboardParticleEmitter>();
  bossDustParticleGroup_->Initialize("resources/circle.png");
  bossDustEmitter_ = std::make_unique<ParticleEmitter>(
      bossDustParticleGroup_.get(), Vector3{0, 0, 0}, Vector3{8.0f, 8.0f, 8.0f},
      6, 0.05f, Vector3{0.0f, 0.6f, 0.0f}, Vector3{6.0f, 6.0f, 6.0f}, 1.0f,
      2.5f);
  bossDustEmitter_->SetBaseScale({0.8f, 0.8f, 0.8f});
  bossDustEmitter_->SetScaleRandom({0.4f, 0.4f, 0.4f});
  bossDustEmitter_->SetScaleVelocity({-0.1f, -0.1f, -0.1f});
  bossDustEmitter_->SetColor({0.2f, 1.5f, 2.0f, 0.8f});

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
  skybox_->SetColor({0.6f, 0.6f, 0.6f, 1.0f});

  //===========================
  // プレイヤーの初期化
  //===========================
  ModelManager::GetInstance()->LoadModel("suzanne.obj");
  ModelManager::GetInstance()->LoadModel("player_dragon.obj");

  player_ = std::make_unique<Player>(railCamera_.get());
  player_->SetSpriteRenderer(engine_->GetSpriteRenderer());
  player_->SetObject3dRenderer(engine_->GetObject3dRenderer());
  player_->SetInput(engine_->GetInputManager());
  player_->SetHitStopCallback([this](int frames) { RequestHitStop(frames); });

  // プレイヤーの初期化（照準やコライダーの生成など）
  player_->Initialize();
  auto playerModel = std::make_unique<Object3d>();
  playerModel->Initialize(engine_->GetObject3dRenderer());
  playerModel->SetModel("player_dragon.obj"); // 正式自機モデル
  playerModel->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
  player_->SetModel(std::move(playerModel));


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
  if (auto hpNode = UIManager::GetInstance()->GetNodeByName("HPBarImage")) {
    hpBarBaseWidth_ = hpNode->scale.x;
  }

  // リボントレイル用テクスチャのロード
  TextureManager::GetInstance()->LoadTexture("resources/gradationLine.png");
}

void GamePlayScene::Finalize() {
  SoundManager::GetInstance()->StopBGM();
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
  } else if (!isPlayMode_ && previousGlobalPlayMode_) {
    isPaused_ = false;
    useDebugCamera_ = true;
    LoadLevel("level_editor_temp.json");

    // ゲーム状態のリセット
    gameState_ = GameState::Play;
    UIManager::GetInstance()->Load("resources/UI/GamePlayUI.json");

    // 残っている敵や弾をクリア
    runtimeEnemies_.clear();
    ActorManager::GetInstance()->Clear();
    CollisionManager::GetInstance()->Clear(); // コライダー残留バグ対策

    // ボス関連フラグのリセット（これがないと2回目のPlayでEmitが呼ばれない）
    hasBossStartedDying_ = false;

    // プレイヤーのステータスを初期化
    if (player_) {
      player_->SetLoadConfigOnInitialize(false);
      player_->Initialize();
    }

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

  // HPバーUI
  if (player_) {
    float playerMaxHp = static_cast<float>(player_->GetMaxHp());
    float playerCurrentHp = static_cast<float>(player_->GetHp());

    // Hp割合計算
    float hpRatio = (std::max)(playerCurrentHp / playerMaxHp, 0.0f);

    if (auto hpNode = UIManager::GetInstance()->GetNodeByName("HPBarImage")) {
      hpNode->scale.x = hpBarBaseWidth_ * hpRatio;

      // HPが3割以下になったら赤色にする
      if (hpRatio <= 0.3f) {
        hpNode->color = {1.0f, 0.0f, 0.0f, 1.0f}; // 赤色
      } else {
        hpNode->color = {0.0f, 1.0f, 0.0f, 1.0f}; // 緑色
      }
    }
  }

  // デバッグカメラ切り替え
  if (engine_->GetInputManager()->IsTriggerKey(DIK_P)) {
    if (useDebugCamera_) {
      useDebugCamera_ = false;
    } else {
      useDebugCamera_ = true;
    }
  }

  //=======================
  // ゲーム進行状態の更新
  //=======================
  if (gameState_ == GameState::Play) {
    if (player_ && player_->IsDead()) {
      gameState_ = GameState::GameOver;
      if (railCamera_)
        railCamera_->SetAutoMove(false);
      UIManager::GetInstance()->Load("resources/UI/GameOverUI.json");
    } else if (railCamera_ && railCamera_->IsFinished()) {
      // ボスが生きている場合はレールが終わってもクリアにしない
      bool isBossActive = false;
      for (const auto &enemy : runtimeEnemies_) {
        if (dynamic_cast<Boss *>(enemy.get())) {
          isBossActive = true;
          break;
        }
      }

      if (!isBossActive) {
        gameState_ = GameState::Clear;
        SoundManager::GetInstance()->StopBGM();
        SoundManager::GetInstance()->PlaySE("victory_jingle");
        if (railCamera_)
          railCamera_->SetAutoMove(false);
        UIManager::GetInstance()->Load("resources/UI/ClearUI.json");
      }
    }
  } else if (gameState_ == GameState::Clear ||
             gameState_ == GameState::GameOver) {
    // リザルト画面でEnterキーを押したらステージセレクト（またはタイトル）へ戻る
    if (engine_->GetInputManager()->IsTriggerKey(DIK_RETURN)) {
      SceneManager::GetInstance()->SetNextTransitionFade(0.5f);
      SceneManager::GetInstance()->ChangeScene(
          gameState_ == GameState::Clear ? "STAGE_SELECT" : "TITLE");
    }
  }

  if (hitStopTimer_ > 0) {
    hitStopTimer_--;
  }
  if (hitStopCooldownTimer_ > 0) {
    hitStopCooldownTimer_--;
  }

  bool shouldUpdateWorld =
      (isPlayMode_ && !isPaused_ && hitStopTimer_ <= 0) || doStep_;

  if (doStep_) {
    doStep_ = false;
  }

  //=======================
  // カメラの更新
  //=======================
  const ICamera *activeCamera = nullptr;

  // RailCameraにプレイヤーの現在位置（前フレーム座標）を渡す
  if (player_ && railCamera_) {
    railCamera_->SetPlayerWorldPosition(player_->GetTransform().translate);
  }

  if (useDebugCamera_) {
    debugCamera_->Update(*engine_->GetInputManager());
    activeCamera = debugCamera_->GetCamera();
  } else {
    if (!shouldUpdateWorld) {
      bool autoMoveCache = railCamera_->GetAutoMove();
      railCamera_->SetAutoMove(false);
      railCamera_->Update();
      railCamera_->SetAutoMove(autoMoveCache);
    } else {
      railCamera_->Update();
    }
    activeCamera = railCamera_.get();
  }

  // ポストエフェクトとエフェクトマネージャーの更新
  if (postProcess_) {
    EffectManager::GetInstance()->Update(activeCamera);
  }

  // スポナーロジック
  if (railCamera_) {
    float t = railCamera_->GetT();
    for (auto &ev : spawnEvents_) {
      if (t < ev.spawnTime) {
        ev.hasSpawned = false; // シークバック時にフラグをリセット
      } else if (shouldUpdateWorld && !ev.hasSpawned && t >= ev.spawnTime) {
        // スポーン位置の計算
        Vector3 spawnWorldPos;
        if (ev.isWorldSpace) {
          // ワールド絶対座標指定（固定砲台・環境物など）
          spawnWorldPos = ev.spawnOffset;
        } else {
          // カメラ相対座標指定（指定時間tの「レールの基準座標」を用いて計算）
          Vector3 cameraPos = railCamera_->GetRailPosition();
          Vector3 cameraRight = railCamera_->GetRailRight();
          Vector3 cameraUp = railCamera_->GetRailUp();
          Vector3 cameraForward = railCamera_->GetRailForward();

          spawnWorldPos = cameraPos +
                          Vector3{cameraRight.x * ev.spawnOffset.x,
                                  cameraRight.y * ev.spawnOffset.x,
                                  cameraRight.z * ev.spawnOffset.x} +
                          Vector3{cameraUp.x * ev.spawnOffset.y,
                                  cameraUp.y * ev.spawnOffset.y,
                                  cameraUp.z * ev.spawnOffset.y} +
                          Vector3{cameraForward.x * ev.spawnOffset.z,
                                  cameraForward.y * ev.spawnOffset.z,
                                  cameraForward.z * ev.spawnOffset.z};
        }

        // 敵の生成
        auto newEnemy = PrefabManager::GetInstance()->InstantiateEnemy(
            ev.prefabName,
            Transform{{3.0f, 3.0f, 3.0f}, {0, 0, 0}, spawnWorldPos});

        Enemy *enemyPtr = newEnemy.get();

        std::unique_ptr<IEnemyBehavior> behavior;
        switch (static_cast<int>(ev.moveType)) {
        case 0: behavior = std::make_unique<BehaviorStraight>(); break;
        case 2: behavior = std::make_unique<BehaviorSineWave>(); break;
        case 3: behavior = std::make_unique<BehaviorStationary>(); break;
        case 4: behavior = std::make_unique<BehaviorFighter>(); break;
        case 5: behavior = std::make_unique<BehaviorMeteor>(); break;
        case 6: behavior = std::make_unique<BehaviorStrafe>(); break;
        case 7: behavior = std::make_unique<BehaviorTurret>(); break;
        default: behavior = std::make_unique<BehaviorStraight>(); break;
        }
        enemyPtr->SetMoveType(ev.moveType);
        enemyPtr->SetMoveDirection(ev.moveDirection);
        enemyPtr->SetBehavior(std::move(behavior));

        if (!ev.splineName.empty() && loadedSplines_.count(ev.splineName)) {
          enemyPtr->SetBehavior(std::make_unique<BehaviorSpline>(
              loadedSplines_[ev.splineName], ev.splineDuration,
              ev.isWorldSpaceSpline, ev.fireInterval));
        }

        if (ev.prefabName == "Boss") {
          if (auto boss = dynamic_cast<Boss *>(enemyPtr)) {
            boss->InitializeUI(engine_->GetSpriteRenderer());
            boss->SetCamera(railCamera_.get());
            boss->SetPlayer(player_.get());
            boss->GetTransform().scale = {10.0f, 10.0f, 10.0f}; // さらに巨大化

            boss->SetOnDyingUpdateCallback([this](const Vector3 &pos) {
              if (!hasBossStartedDying_) {
                hasBossStartedDying_ = true;
                bossExplosionEmitter_->SetCenter(pos);
                bossExplosionEmitter_->Emit();
                SoundManager::GetInstance()->PlaySE("boss_explosion");
                if (railCamera_)
                  railCamera_->Shake(1.5f, 0.4f);
              }
              bossDustEmitter_->SetCenter(pos);
              bossDustEmitter_->Update();
            });

            // ボス戦開始: レールカメラを低速化し、ボスBGMへ切り替え
            SoundManager::GetInstance()->PlayBGM("boss_bgm");
            if (railCamera_) {
              railCamera_->SetSpeed(0.05f);
            }
          }
        }

        // ボス撃破時はクリア画面へ移行するコールバックを登録
        if (ev.prefabName == "Boss") {
          newEnemy->SetOnDestroyedCallback([this](bool isSelfDestruct) {
            gameState_ = GameState::Clear;
            SoundManager::GetInstance()->StopBGM();
            SoundManager::GetInstance()->PlaySE("victory_jingle");
            if (railCamera_)
              railCamera_->SetAutoMove(false);
            UIManager::GetInstance()->Load("resources/UI/ClearUI.json");

            if (auto scoreNode =
                    UIManager::GetInstance()->GetNodeByName("ScoreText")) {
              if (player_) {
                player_->AddScore(10000); // ボス撃破スコアを加算
                char scoreBuf[64];
                snprintf(scoreBuf, sizeof(scoreBuf), "SCORE: %06d",
                         player_->GetScore());
                scoreNode->textString = scoreBuf;
              }
            }
          });
        }

        // カメラとオフセット、プレイヤー情報をセット
        newEnemy->SetCamera(railCamera_.get());
        newEnemy->SetPlayer(player_.get());
        newEnemy->SetSpawnOffset(ev.spawnOffset);

        auto prevCallback = newEnemy->GetOnDestroyedCallback();
        newEnemy->SetOnDestroyedCallback(
            [this, prevCallback](bool isSelfDestruct) {
              if (!isSelfDestruct) {
                if (player_) {
                  player_->AddScore(100);
                }
              }
              if (prevCallback) {
                prevCallback(isSelfDestruct);
              }
            });

        // Playモード時のみ実際の敵を生成
        if (isPlayMode_) {
          runtimeEnemies_.push_back(std::move(newEnemy));
        }
        ev.hasSpawned = true;
      }
    }
  }

  // 基底クラスにも現在のアクティブカメラを教える（Gizmo描画などで使うため）
  SetActiveCamera(const_cast<ICamera *>(activeCamera));

  // 敵の更新 (Playモードで生成された敵のみ)
  for (auto &enemy : runtimeEnemies_) {
    if (shouldUpdateWorld) {
      enemy->Update();
    } else {
      enemy->UpdateTransform();
    }
  }
  for (auto &obj : sceneObjects_) {
    obj->Update();
  }

  // LockOn用にPlayerに対象リストを渡す（毎回最新の状態を渡す）
  // 死亡済みの敵は除外してダングリングポインタを渡さないようにする
  std::vector<BaseActor *> lockOnTargets;
  for (auto &e : runtimeEnemies_) {
    if (!e->IsDead() && e->IsLockOnTarget()) {
      lockOnTargets.push_back(e.get());
    }
  }

  // 敵の弾（ロックオン対象としてタグ付けされたもの）もリストに加える
  std::vector<BaseActor *> bulletTargets =
      ActorManager::GetInstance()->FindActorsWithTag(ActorTag::LockOnTarget);
  lockOnTargets.insert(lockOnTargets.end(), bulletTargets.begin(),
                       bulletTargets.end());

  if (player_) {
    player_->SetLockOnTargets(lockOnTargets);
  }

  // 死亡済みの敵を削除（デストラクタ内でコライダーも自動登録解除される）
  if (shouldUpdateWorld) {
    runtimeEnemies_.erase(std::remove_if(runtimeEnemies_.begin(),
                                         runtimeEnemies_.end(),
                                         [](const std::unique_ptr<Enemy> &e) {
                                           return e->IsDead();
                                         }),
                          runtimeEnemies_.end());
  }

  // アクティブカメラを描画で使用する
  engine_->GetObject3dRenderer()->SetDefaultCamera(activeCamera);

  if (skybox_) {
    skybox_->SetCamera(activeCamera);
    skybox_->Update();
  }


  //===========================================
  // プレイヤーの更新
  //===========================================

  if (player_) {
    // プレイヤーの照準や挙動の計算には常にレールカメラを使用する
    if (shouldUpdateWorld) {
      player_->Update();
    } else {
      player_->UpdateTransform();
    }


  }

  // アクター群の更新
  if (shouldUpdateWorld) {
    ActorManager::GetInstance()->Update();
  }

  // 当たり判定の更新
  if (shouldUpdateWorld) {
    CollisionManager::GetInstance()->Update();
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
      bool isDirty = player_->IsActionConfigDirty();

      if (isDirty) {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            ImVec4(0.8f, 0.6f, 0.0f, 1.0f)); // 警告色（オレンジ）
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
      }

      std::string buttonText = isDirty
                                   ? (const char *)u8"[* 未保存] Save Config"
                                   : (const char *)u8"Save Config";
      if (ImGui::Button(buttonText.c_str(),
                        ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
        player_->SaveActionConfig();
        isDirty = false;
      }

      if (isDirty) {
        ImGui::PopStyleColor(3);
      }

      ImGui::Spacing();

      auto &config = player_->GetActionConfig();
      bool changed = false;

      changed |= ImGui::SliderFloat((const char *)u8"ロックオンの吸いつき範囲",
                                    &config.lockOnRadius, 10.0f, 500.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ホーミング弾のスピード",
                                    &config.homingSpeed, 0.1f, 5.0f);
      changed |=
          ImGui::SliderInt((const char *)u8"追尾を開始するまでのフレーム",
                           &config.homingFallTime, 0, 300);
      changed |=
          ImGui::SliderFloat((const char *)u8"追尾のカーブの鋭さ",
                             &config.homingStrengthIncrease, 0.001f, 0.1f);
      changed |= ImGui::SliderFloat((const char *)u8"旋回力（追尾力）",
                                    &config.homingStrengthMax, 0.01f, 1.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ホーミング弾の左右拡散幅",
                                    &config.homingSpreadX, 0.0f, 2.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ホーミング弾の上方初速",
                                    &config.homingSpeedY, 0.0f, 5.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ホーミング弾の前方初速",
                                    &config.homingSpeedZ, 0.0f, 5.0f);
      changed |= ImGui::SliderFloat((const char *)u8"照準の加速度",
                                    &config.reticleAcceleration, 0.1f, 10.0f);
      changed |= ImGui::SliderFloat((const char *)u8"照準の摩擦力",
                                    &config.reticleFriction, 0.5f, 0.99f);
      changed |= ImGui::SliderFloat((const char *)u8"照準の最高速度",
                                    &config.reticleMaxSpeed, 1.0f, 100.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ロール（バンク）の強さ",
                                    &config.rollStrength, 0.0f, 20.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ピッチ（上下）の追従強度",
                                    &config.pitchStrength, 0.0f, 10.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ヨー（首振り）の追従強度",
                                    &config.yawStrength, 0.0f, 10.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ロールの補間速度（Lerp）",
                                    &config.rollLerp, 0.01f, 1.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ピッチの補間速度（Lerp）",
                                    &config.pitchLerp, 0.01f, 1.0f);
      changed |= ImGui::SliderFloat((const char *)u8"ヨーの補間速度（Lerp）",
                                    &config.yawLerp, 0.01f, 1.0f);
      changed |= ImGui::SliderFloat((const char *)u8"通常弾のスピード",
                                    &config.normalShotSpeed, 1.0f, 50.0f);
      changed |= ImGui::DragFloat((const char *)u8"口元の左右オフセット (X)",
                                  &config.muzzleOffsetX, 0.05f, -10.0f, 10.0f);
      changed |= ImGui::DragFloat((const char *)u8"口元の上方オフセット (Y)",
                                  &config.muzzleOffsetY, 0.05f, -10.0f, 10.0f);
      changed |= ImGui::DragFloat((const char *)u8"口元の前方オフセット (Z)",
                                  &config.muzzleOffsetForward, 0.05f, -10.0f, 10.0f);

      if (changed) {
        player_->SetActionConfigDirty(true);
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
  } else if (currentSelectType_ == EditorSelectType::RailCamera) {
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

    const char *tagItems[] = {"Untagged", "Player", "Enemy", "LockOnTarget"};
    int currentItem = static_cast<int>(selected->tag_);
    if (ImGui::Combo("Tag", &currentItem, tagItems, IM_ARRAYSIZE(tagItems))) {
      selected->tag_ = static_cast<ActorTag>(currentItem);
    }
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
    if (ImGui::Combo("Move Type", &currentMoveType, moveTypes, IM_ARRAYSIZE(moveTypes))) {
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
          if (ev.fireInterval < -1) ev.fireInterval = -1; // -1を最小値とする
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
    ImGui::TextDisabled("World Pos: (%.1f, %.1f, %.1f)", worldPos.x,
                        worldPos.y, worldPos.z);

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

  // レールのデバッグ描画（スプライン曲線をサンプリングして描画）
  const auto &waypoints = railCamera_->GetWaypoints();
  if (waypoints.size() > 1) {
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
      for (auto &enemy : runtimeEnemies_) {
        if (Boss *boss = dynamic_cast<Boss *>(enemy.get())) {
          boss->TakeDamage(99999);
        }
      }
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text(" | ");
  }
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

#endif
}

void GamePlayScene::Draw() { Draw3D(); }

void GamePlayScene::Draw3D() {
  engine_->Begin3D();

  if (skybox_) {
    engine_->GetSkyboxRenderer()->Begin();
    skybox_->Draw();

    // Skybox描画後に通常3Dへ戻す
    engine_->GetObject3dRenderer()->Begin();
  }

  // 敵の描画
  for (auto &enemy : runtimeEnemies_) {
    if (!enemy->IsDead()) {
      enemy->Draw3D();
    }
  }
  for (auto &obj : sceneObjects_) {
    obj->Draw();
  }

  // プレビューオブジェクトの描画
  if (isPreviewHovering_ && previewObject_) {
    previewObject_->Draw();
  }


  if (player_) {
    player_->Draw3D();
  }

  // アクター群（弾など）の描画
  ActorManager::GetInstance()->Draw3D();

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
    bossExplosionParticleGroup_->Update(activeCamera->GetViewMatrix(),
                                        activeCamera->GetProjectionMatrix());
    bossDustParticleGroup_->Update(activeCamera->GetViewMatrix(),
                                   activeCamera->GetProjectionMatrix());
  }
  ParticleManager::GetInstance()->Emit();

  EffectManager::GetInstance()->Draw();

  bossExplosionParticleGroup_->Draw();
  bossDustParticleGroup_->Draw();

  // リボントレイル（弾やミサイルの光の軌跡）の一括描画
  if (activeCamera) {
    auto srvHandle = TextureManager::GetInstance()->GetSrvHandleGPU("resources/gradationLine.png");
    TrailRenderer::GetInstance()->Render(activeCamera->GetViewProjectionMatrix(), srvHandle);
  }

  engine_->End3D();
}

void GamePlayScene::Draw2D() {
  if (player_) {
    // リザルト画面中はレティクルを消す
    if (gameState_ == GameState::Play) {
      player_->Draw2D();
    }
  }

  // 敵の2D描画（ボスのUIなど）
  if (gameState_ == GameState::Play) {
    for (auto &enemy : runtimeEnemies_) {
      if (!enemy->IsDead()) {
        enemy->Draw2D();
      }
    }
  }

  // スプライト（UI）の描画
  UIManager::GetInstance()->Draw();

#ifdef USE_IMGUI
  if (gameState_ == GameState::Clear) {

    // EnterでStageSelectSceneへ帰還
    if (engine_->GetInputManager()->IsTriggerKey(DIK_RETURN)) {
      SceneManager::GetInstance()->SetNextTransitionFade(0.5f);
      SceneManager::GetInstance()->ChangeScene("STAGE_SELECT");
    }
  } else if (gameState_ == GameState::GameOver) {

    // EnterでStageSelectSceneへ帰還
    if (engine_->GetInputManager()->IsTriggerKey(DIK_RETURN)) {
      SceneManager::GetInstance()->SetNextTransitionFade(0.5f);
      SceneManager::GetInstance()->ChangeScene("STAGE_SELECT");
    }
  }
#endif
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
    oJson["tag"] = ActorTagToString(obj->tag_);
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

  // カメラレール（waypoints）を camera_rail.json に別途保存
  nlohmann::json waypointsRoot;
  nlohmann::json waypointsArray = nlohmann::json::array();
  if (railCamera_) {
    for (const auto &wp : railCamera_->GetWaypoints()) {
      waypointsArray.push_back({wp.x, wp.y, wp.z});
    }
  }
  waypointsRoot["waypoints"] = waypointsArray;
  std::ofstream railFile("resources/levels/camera_rail.json");
  if (railFile.is_open()) {
    railFile << std::setw(4) << waypointsRoot << std::endl;
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
  runtimeEnemies_.clear();
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
        ev.moveDirection = {0.0f, 0.0f, 1.0f}; // デフォルトは前進（互換性維持）
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
      if (oJson.contains("tag"))
        obj->tag_ = StringToActorTag(oJson["tag"]);
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
    } catch (const nlohmann::json::parse_error &e) {
      Logger::Log(
          std::string("[GamePlayScene] camera_rail.json parse error: ") +
          e.what());
    }
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
