#include "DebugScene.h"
#include "Actor/Enemy.h"
#include "Actor/Player.h"
#include "Camera/GameCamera.h"
#include "Camera/RailCamera.h"
#include "Collision/CollisionManager.h"
#include "Debug/ImGuiManager.h"
#include "Debug/Logger.h"
#include "Framework/ActorManager.h"
#include "Input/Input.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Particle/Particle.h"
#include "Particle/ParticleManager.h"
#include "Render/Primitive/Sphere.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/PostProcess.h"
#include "Renderer/SpriteRenderer.h"
#include "Scene/SceneManager.h"
#include "Texture/TextureManager.h"
#include <cmath>
#include <fstream>
#include <imgui.h>

void DebugScene::Initialize(EngineBase *engine) {

  BaseScene::Initialize(engine);
  engine_ = engine;

  // マネージャーを初期化して、テスト用のプレイヤーを1人放り込む
  ActorManager::GetInstance()->Initialize();

  // PostProcess用テクスチャ
  TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
  TextureManager::GetInstance()->LoadTexture("resources/noise1.png");
  noise0TextureIndex_ =
      TextureManager::GetInstance()->GetSrvIndex("resources/noise0.png");
  noise1TextureIndex_ =
      TextureManager::GetInstance()->GetSrvIndex("resources/noise1.png");

  //===========================
  // テクスチャファイルの読み込み
  //===========================

  // テクスチャの読み込み
  // TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

  // TextureManager::GetInstance()->LoadTexture("resources/monsterball.png");

  //===========================
  // オーディオファイルの読み込み
  //===========================

  auto *sm = SoundManager::GetInstance();

  // Audioファイルを登録
  sm->Load("mokugyo", "resources/mokugyo.wav");
  sm->Load("se", "resources/fanfare.wav");

  // bgm再生
  sm->PlayBGM("mokugyo");

  //===========================
  // スプライト関係の初期化
  //===========================

  // スプライトの生成と初期化
  for (int i = 0; i < kSpriteCount_; ++i) {
    auto sprite = std::make_unique<Sprite>();

    if (i == 0) {
      sprite->Initialize(engine_->GetSpriteRenderer(),
                         "resources/white1x1.png");
      // シェーダーで暗い文字として判定・加算されるように、マテリアルカラーを暗いブルーに設定
      sprite->SetColor(Vector4{0.02f, 0.05f, 0.3f, 1.0f});
    } else if (i % 2 == 1) {
      sprite->Initialize(engine_->GetSpriteRenderer(),
                         "resources/white1x1.png");
    } else {
      sprite->Initialize(engine_->GetSpriteRenderer(),
                         "resources/monsterBall.png");
    }
    sprites_.push_back(std::move(sprite));
  }

  sprite_ = std::make_unique<Sprite>();
  sprite_->Initialize(engine_->GetSpriteRenderer(), "resources/white1x1.png");

  for (uint32_t i = 0; i < kSpriteCount_; ++i) {
    spritePositions_[i] = {i * 200.0f, 0.0f};
    spriteSizes_[i] = {150.0f, 150.0f};
  }

  // スプライトのTransform
  spritePosition_ = {
      100.0f,
      100.0f,
  };

  spriteRotation_ = 0.0f;

  spriteColor_ = {1.0f, 1.0f, 1.0f, 1.0f};

  spriteSize_ = {640.0f, 360.0f};

  spriteAnchorPoint_ = {0.0f, 0.0f};

  sprite_->SetPosition(spritePosition_);
  sprite_->SetRotation(spriteRotation_);
  sprite_->SetColor(spriteColor_);
  sprite_->SetSize(spriteSize_);
  sprite_->SetAnchorPoint(spriteAnchorPoint_);
  sprite_->SetIsFlipX(isFlipX_);
  sprite_->SetIsFlipY(isFlipY_);

  // UVTransform用
  uvTransformSprite_ = {
      {1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 0.0f},
  };

  //===========================
  // 3Dオブジェクト関係の初期化
  //===========================

  // カメラの生成と初期化
  camera_ = std::make_unique<GameCamera>();
  camera_->SetRotate({0.3f, 0.0f, 0.0f});
  camera_->SetTranslate({0.0f, 4.0f, -10.0f});

  // レールカメラの初期化
  waypoints_ = {{0.0f, 4.0f, -10.0f},  {0.0f, 4.0f, 10.0f},
                {15.0f, 4.0f, 20.0f},  {0.0f, 10.0f, 40.0f},
                {-15.0f, 4.0f, 60.0f}, {0.0f, 4.0f, 80.0f}};
  railCamera_ = std::make_unique<RailCamera>();
  railCamera_->Initialize(waypoints_);
  railCamera_->SetSpeed(0.5f); // 1秒間に0.5セグメント進む

  // デフォルトカメラをレールカメラに設定
  engine_->GetObject3dRenderer()->SetDefaultCamera(railCamera_.get());

  // === Playerの初期化をここへ移動（railCamera_が生成された後に行う） ===
  // 自機用のモデルをロードしておく
  ModelManager::GetInstance()->LoadModel("suzanne.obj");

  auto player = std::make_unique<Player>(railCamera_.get());
  player->SetSpriteRenderer(engine_->GetSpriteRenderer());
  player->SetObject3dRenderer(engine_->GetObject3dRenderer()); // 追加
  player->SetInput(engine_->GetInputManager());                // Inputを渡す

  auto playerModel = std::make_unique<Object3d>();
  playerModel->Initialize(engine_->GetObject3dRenderer());
  playerModel->SetModel("suzanne.obj"); // 仮の自機モデル
  playerModel->SetColor(
      {0.0f, 0.5f, 1.0f, 1.0f}); // プレイヤーは青色で分かりやすく
  player->SetModel(std::move(playerModel));

  playerPtr_ = player.get(); // ポインタを控えておく
  ActorManager::GetInstance()->AddActor(std::move(player));

  cameraTransform_ = {
      {1.0f, 1.0f, 1.0f},
      {0.3f, 0.0f, 0.0f},
      {0.0f, 4.0f, -10.0f},
  };

  // デバッグカメラ
  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize({0.0f, 4.0f, -10.0f});

  // モデルの読み込み
  ModelManager::GetInstance()->LoadModel("terrain.obj");

  ModelManager::GetInstance()->LoadModel("suzanne.obj");

  ModelManager::GetInstance()->LoadModel("AnimatedCube.gltf");

  // 【テスト用】Enemyの配置
  // auto testEnemy = std::make_unique<Enemy>();
  // testEnemy->GetTransform().translate = {0.0f, 0.0f, 5.0f};
  // auto enemyModel = std::make_unique<Object3d>();
  // enemyModel->Initialize(engine_->GetObject3dRenderer());
  // enemyModel->SetModel("suzanne.obj");
  // testEnemy->SetModel(std::move(enemyModel));
  // ActorManager::GetInstance()->AddActor(std::move(testEnemy));

  // オブジェクトの生成と初期化
  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(engine_->GetObject3dRenderer());
  object3d_->SetModel("terrain.obj");

  // 複数のテスト用敵（スザンヌ）を扇状に配置する
  for (int i = 0; i < 5; ++i) {
    auto target = std::make_unique<Object3d>();
    target->Initialize(engine_->GetObject3dRenderer());
    target->SetModel("suzanne.obj");

    // X座標をずらして横に並べ、Y座標で少し高低差をつける
    float posX = -10.0f + (i * 5.0f);
    float posY = 4.0f + (i % 2) * 2.0f;
    target->SetTranslation({posX, posY, 35.0f});
    target->SetColor(Vector4{1.0f, 1.0f, 0.0f, 1.0f});

    testTargets_.push_back(std::move(target));
  }

  animatedCube_ = std::make_unique<Object3d>();
  animatedCube_->Initialize(engine_->GetObject3dRenderer());
  animatedCube_->SetModel("AnimatedCube.gltf");
  animation_ = LoadAnimationFile("resources/AnimatedCube", "AnimatedCube.gltf");
  animatedCube_->PlayAnimation(animation_);
  animatedCube_->SetTranslation({-3.0f, 1.0f, 0.0f});

  // ==========================
  // スケルトンのデバッグ描画用初期化
  // ==========================
  sneakWalk_ = std::make_unique<SkinnedObject>();
  sneakWalk_->Initialize(engine_->GetObject3dRenderer(),
                         engine_->GetSrvManager(), "resources/human",
                         "sneakWalk.gltf");

  Sphere sphere;
  sphere.Build(16, 0.02f);

  std::vector<Model::VertexData> sphereVertices;
  for (const auto &v : sphere.GetVertices()) {
    sphereVertices.push_back({v.position, v.texcoord, v.normal});
  }

  sphereModel_ = std::make_unique<Model>();
  sphereModel_->InitializeFromVertices(
      ModelManager::GetInstance()->GetModelRenderer(), sphereVertices);

  jointObjects_.clear();
  for (size_t i = 0; i < sneakWalk_->GetSkeleton().joints.size(); ++i) {
    auto jointObj = std::make_unique<Object3d>();
    jointObj->Initialize(engine_->GetObject3dRenderer());
    jointObj->SetModel(sphereModel_.get());
    jointObj->SetColor(
        Vector4{1.0f, 0.0f, 0.0f, 1.0f}); // 分かりやすいように赤色にする
    jointObjects_.push_back(std::move(jointObj));
  }

  //===========================
  // SkyBox
  //===========================
  TextureManager::GetInstance()->LoadTexture("resources/Skybox/Skybox.dds");

  skybox_ = std::make_unique<Skybox>();
  skybox_->Initialize(engine_->GetSkyboxRenderer());
  skybox_->SetTexture("resources/Skybox/Skybox.dds");
  skybox_->SetCamera(camera_.get());
  skybox_->SetScale({100.0f, 100.0f, 100.0f});

  //===========================
  // パーティクル関係の初期化
  //===========================

  // グループ（実体）の作成と初期化
  testParticleGroup_ = std::make_unique<BillboardParticleEmitter>();
  testParticleGroup_->Initialize("resources/circle.png");

  clearParticleGroup_ = std::make_unique<BillboardParticleEmitter>();
  clearParticleGroup_->Initialize("resources/uvChecker.png");

  hitParticleGroup_ = std::make_unique<MeshParticleEmitter>();
  hitParticleGroup_->InitializeAsRing("resources/white1x1.png", 64, 1.0f, 0.2f);

  cylinderParticleGroup_ = std::make_unique<MeshParticleEmitter>();
  cylinderParticleGroup_->InitializeAsCylinder("resources/gradationLine.png",
                                               32, 1.0f, 1.0f, 3.0f);

  planeHitParticleGroup_ = std::make_unique<MeshParticleEmitter>();
  planeHitParticleGroup_->InitializeAsPlane("resources/uvChecker.png");

  // 中心からXY方向にランダム速度
  particleEmitter_ = std::make_unique<ParticleEmitter>(
      testParticleGroup_.get(),  // 対象グループ
      Vector3{0.0f, 0.0f, 5.0f}, // center (エミッターの中心座標)
      Vector3{0.2f, 0.2f, 0.2f}, // halfSize（エミッターの半径）
      3,                         // 一度に発生させる数
      1.0f,                      // 発生間隔
      Vector3{0.0f, 0.0f, 0.0f}, // baseVel
      Vector3{1.0f, 1.0f, 1.0f}, // velRandom
      1.6f,                      // lifeMin
      2.1f                       // lifeMax
  );

  // 噴水のように下から吹き上げる
  clearParticleEmitter_ = std::make_unique<ParticleEmitter>(
      clearParticleGroup_.get(), Vector3{1.0f, 0.0f, 2.0f},
      Vector3{0.2f, 0.0f, 0.2f}, 8, 1.0f, Vector3{0.0f, 6.0f, 0.0f},
      Vector3{1.5f, 1.0f, 1.5f}, 0.6f, 1.1f);

  hitParticleEmitter_ = std::make_unique<ParticleEmitter>(
      hitParticleGroup_.get(), Vector3{0.0f, 2.0f, 0.0f}, Vector3{}, 8, 3.0f,
      Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.55f);

  hitParticleEmitter_->SetBaseScale(Vector3{0.05f, 1.0f, 1.0f});
  hitParticleEmitter_->SetRotateRandom(Vector3{0.0f, 0.0f, 2.0f});

  cylinderEmitter_ = std::make_unique<ParticleEmitter>(
      cylinderParticleGroup_.get(), Vector3{0.0f, 0.0f, 0.0f}, Vector3{}, 1,
      99999.0f, Vector3{0, 0, 0}, Vector3{0, 0, 0}, 99999.0f, 99999.0f);
  cylinderEmitter_->SetBaseScale(Vector3{1.0f, 1.0f, 1.0f});
  cylinderEmitter_->Emit();

  planeHitParticleEmitter_ = std::make_unique<ParticleEmitter>(
      planeHitParticleGroup_.get(), Vector3{-2.0f, 2.0f, 0.0f}, Vector3{}, 3,
      1.5f, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 1.0f);
  planeHitParticleEmitter_->SetBaseScale(Vector3{2.0f, 2.0f, 1.0f});
  planeHitParticleEmitter_->SetRotateRandom(Vector3{0.0f, 0.0f, 3.14f});

  //===========================
  // レベルデータの読み込みと配置
  //===========================
  levelData_ = std::make_unique<LevelData>();

  // Blenderから出力した testScene.json が存在するか確認してから読み込む
  std::ifstream levelFile("resources/testScene.json");
  if (levelFile.is_open()) {
    levelFile.close();
    levelData_->LoadFile("resources/testScene.json");

    // ツリー構造を再帰的に生成
    for (auto &objectData : levelData_->objects) {
      CreateObjectsRecursive(objectData, nullptr);
    }

    // プレイヤー配置データからプレイヤーを配置
    if (!levelData_->players.empty()) {
      auto &playerData = levelData_->players[0];
      if (sneakWalk_) {
        sneakWalk_->GetObject3d()->SetTranslation(playerData.translation);
        sneakWalk_->GetObject3d()->SetRotation(playerData.rotation);
      }
    }

    // レベルデータから敵を生成、配置
    for (auto &enemyData : levelData_->enemies) {
      // 敵キャラの生成
      auto enemy = std::make_unique<Object3d>();
      enemy->Initialize(engine_->GetObject3dRenderer());

      // fileNameがあれば読み込んでセット、なければダミーとしてsuzanne.objをセット
      if (!enemyData.fileName.empty()) {
        ModelManager::GetInstance()->LoadModel(enemyData.fileName);
        enemy->SetModel(enemyData.fileName);
      } else {
        enemy->SetModel("suzanne.obj");
      }

      // 敵キャラの初期化
      enemy->SetTranslation(enemyData.translation);
      enemy->SetRotation(enemyData.rotation);

      // 敵リストに追加
      enemies_.push_back(std::move(enemy));
    }
  }
}

void DebugScene::CreateObjectsRecursive(const LevelData::ObjectData &objectData,
                                        Object3d *parent) {
  Object3d *newObject = nullptr;

  // ファイル名（モデル名）が設定されていればモデルをロード
  if (!objectData.fileName.empty()) {
    ModelManager::GetInstance()->LoadModel(objectData.fileName);
    auto obj = std::make_unique<Object3d>();
    obj->Initialize(engine_->GetObject3dRenderer());
    obj->SetModel(objectData.fileName);

    // トランスフォームの設定
    obj->SetTranslation(objectData.translation);
    obj->SetRotation(objectData.rotation);
    obj->SetScale(objectData.scaling);

    // 親子関係の設定
    if (parent) {
      obj->SetParent(parent);
    }

    newObject = obj.get();
    levelObjects_.push_back(std::move(obj));
  } else {
    // モデルがない場合でもダミーのトランスフォームノード（親）として機能させるために生成
    auto obj = std::make_unique<Object3d>();
    obj->Initialize(engine_->GetObject3dRenderer());
    obj->SetTranslation(objectData.translation);
    obj->SetRotation(objectData.rotation);
    obj->SetScale(objectData.scaling);

    if (parent) {
      obj->SetParent(parent);
    }

    newObject = obj.get();
    levelObjects_.push_back(std::move(obj));
  }

  // 子オブジェクトの再帰生成
  for (const auto &childData : objectData.children) {
    CreateObjectsRecursive(childData, newObject);
  }
}

void DebugScene::Finalize() {
  testParticleGroup_->ClearParticles();
  clearParticleGroup_->ClearParticles();
  hitParticleGroup_->ClearParticles();
  cylinderParticleGroup_->ClearParticles();
  planeHitParticleGroup_->ClearParticles();

  auto *sm = SoundManager::GetInstance();
  sm->StopBGM();
  sm->StopAllSE();

  // ゲームシーンだけで使う運用ならここで解放してOK
  sm->Unload("mokugyo");
  sm->Unload("se");
}

void DebugScene::Update() {

  // 毎フレームマネージャーに号令をかけさせる
  ActorManager::GetInstance()->Update();

  // 当たり判定の全チェック
  CollisionManager::GetInstance()->Update();

  // input取得
  auto *input = engine_->GetInputManager();

  // Sound更新
  SoundManager::GetInstance()->Update();

  //==============================
  // ゲームシーンに移行
  //==============================

  // フェードはデフォで 0.35秒 & 黒色
  if (engine_->GetInputManager()->IsKeyTrigger(KeyCode::Enter)) {
    // SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
  }

  if (input->IsPadTrigger(PadButton::A)) {
    // SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
  }

  // フェードにかかる時間とフェードタイプと色とイージングを指定できる
  if (engine_->GetInputManager()->IsKeyTrigger(KeyCode::C)) {

    // 0.5秒
    // WipeLeft(左から右へ塗りつぶし)
    // 白色フェード
    // EaseOut
    //   SceneManager::GetInstance()->SetNextTransitionFade(
    //      0.5f, Fade::FadeType::WipeLeft, Vector4{1.0f, 1.0f, 1.0f, 1.0f});

    // SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
  }

  // テクスチャ差し替え
  if (engine_->GetInputManager()->IsTriggerKey(DIK_SPACE)) {
    sprites_[0]->ChangeTexture("resources/uvChecker.png");

    sprites_[1]->SetColor({0.0f, 0.0f, 0.0f, 1.0f});
  }

  // デバッグカメラ切り替え

#ifdef USE_IMGUI

  if (engine_->GetInputManager()->IsTriggerKey(DIK_P)) {
    if (useDebugCamera_) {
      useDebugCamera_ = false;
    } else {
      useDebugCamera_ = true;
    }
  }

#endif // USE_IMGUI

  auto *sm = SoundManager::GetInstance();

  // BGMを停止
  if (engine_->GetInputManager()->IsTriggerKey(DIK_B)) {
    sm->StopBGM();
  }

  // VキーでSE(重複可能）
  if (engine_->GetInputManager()->IsTriggerKey(DIK_V)) {
    SoundManager::GetInstance()->PlaySE("se");
  }

  // Bキーで重複不可のSE
  if (engine_->GetInputManager()->IsKeyTrigger(KeyCode::B)) {
    SoundManager::GetInstance()->PlaySE_Once("se");
  }

  //=======================
  // ディゾルブアニメーション
  //=======================
  if (engine_->GetInputManager()->IsTriggerKey(DIK_1)) {
    suzanneEnableDissolve_ = true;
    suzanneDissolveThreshold_ = 0.0f; // 0から開始
    isPlayingSuzanneDissolve_ = true;
  }

  if (isPlayingSuzanneDissolve_) {
    // フレームごとに閾値を増加
    suzanneDissolveThreshold_ += 0.015f;

    if (suzanneDissolveThreshold_ >= 1.0f) {
      suzanneDissolveThreshold_ = 1.0f;
      isPlayingSuzanneDissolve_ = false; // アニメーション終了
    }
  }

  // 常時ノイズをゆっくりスクロールさせる
  if (suzanneEnableDissolve_) {
    if (isHologramMode_) {
      // ホログラムモード時は縦方向のみ少し速く流す（スキャンライン風）
      suzanneMaskTransform_.y += 0.005f;
    } else {
      suzanneMaskTransform_.x += 0.001f;
      suzanneMaskTransform_.y += 0.001f;
    }
  }

  //=======================
  // スプライトの更新
  //=======================

  // スプライトの情報を呼び出す
  spritePosition_ = sprite_->GetPosition();
  spriteRotation_ = sprite_->GetRotation();
  spriteColor_ = sprite_->GetColor();
  spriteSize_ = sprite_->GetSize();
  spriteAnchorPoint_ = sprite_->GetAnchorPoint();

  // スプライトに設定
  sprite_->SetPosition(spritePosition_);
  sprite_->SetRotation(spriteRotation_);
  sprite_->SetColor(spriteColor_);
  sprite_->SetSize(spriteSize_);
  sprite_->SetAnchorPoint(spriteAnchorPoint_);
  sprite_->SetIsFlipX(isFlipX_);
  sprite_->SetIsFlipY(isFlipY_);

  for (uint32_t i = 0; i < kSpriteCount_; ++i) {
    // spritePositions[i].x++;
    sprites_[i]->SetPosition(spritePositions_[i]);
    sprites_[i]->SetSize(spriteSizes_[i]);
  }

  sprites_[0]->SetScale(Vector2{1.5f, 1.5f});

  sprite_->Update(uvTransformSprite_);

  for (uint32_t i = 0; i < kSpriteCount_; ++i) {
    sprites_[i]->Update(uvTransformSprite_);
  }

  //=======================
  // 3Dオブジェクトの更新
  //=======================

  object3d_->Update();

  for (auto &target : testTargets_) {
    target->Update();
  }

  animatedCube_->Update();

  // レベルデータのオブジェクト更新
  for (auto &obj : levelObjects_) {
    obj->Update();
  }

  // 敵の更新
  for (auto &enemy : enemies_) {
    enemy->Update();
  }

  // スケルトンのアニメーション更新
  sneakWalk_->Update(1.0f / 60.0f);

  for (size_t i = 0; i < sneakWalk_->GetSkeleton().joints.size(); ++i) {
    const Joint &joint = sneakWalk_->GetSkeleton().joints[i];
    Vector3 pos = {joint.skeletonSpaceMatrix.m[3][0],
                   joint.skeletonSpaceMatrix.m[3][1],
                   joint.skeletonSpaceMatrix.m[3][2]};
    jointObjects_[i]->SetTranslation(pos);
    jointObjects_[i]->Update();
  }

  //=======================
  // パーティクルの更新
  //=======================
  ParticleManager::GetInstance()->Update();

  /* エミッタの更新(パーティクルを生成間隔に基づいて自動生成)
  --------------------------------------------------*/
  particleEmitter_->Update();
  clearParticleEmitter_->Update();
  hitParticleEmitter_->Update();
  cylinderEmitter_->Update();
  planeHitParticleEmitter_->Update();

  /* 手動でパーティクルを生成することも可能
  -------------------------------------*/
  if (input->IsKeyTrigger(KeyCode::E)) {

    // デフォルトカラーで生成
    clearParticleEmitter_->Emit();

  } else if (input->IsKeyTrigger(KeyCode::F)) {

    // 色を指定して生成
    clearParticleEmitter_->Emit(Vector4{0.0f, 0.0f, 1.0f, 1.0f});
  }

  //=======================
  // カメラの更新
  //=======================
  const ICamera *activeCamera = nullptr;

  camera_->SetRotate(cameraTransform_.rotate);
  camera_->SetTranslate(cameraTransform_.translate);

  // カメラの更新処理

  // デバッグ機能：Tキーでレールカメラの一時停止/再開
  if (engine_->GetInputManager()->IsTriggerKey(DIK_T)) {
    isRailCameraMoving_ = !isRailCameraMoving_;
  }

  if (useDebugCamera_) {
    debugCamera_->Update(*engine_->GetInputManager());
    activeCamera = debugCamera_->GetCamera();
  } else {
    railCamera_->SetSpeed(isRailCameraMoving_ ? 0.5f : 0.0f);
    railCamera_->Update();
    activeCamera = railCamera_.get();
  }

  // アクティブカメラを描画で使用する
  engine_->GetObject3dRenderer()->SetDefaultCamera(activeCamera);

  // 個別のパーティクルグループを更新
  testParticleGroup_->Update(activeCamera->GetViewMatrix(),
                             activeCamera->GetProjectionMatrix());
  clearParticleGroup_->Update(activeCamera->GetViewMatrix(),
                              activeCamera->GetProjectionMatrix());
  hitParticleGroup_->Update(activeCamera->GetViewMatrix(),
                            activeCamera->GetProjectionMatrix());

  // === Playerにロックオン用の情報を渡す ===
  if (playerPtr_) {
    // DebugCamera 中もプレイヤーはレール上に残す
    playerPtr_->SetCamera(railCamera_.get());

    // 敵のポインタリストを作成して渡す
    // std::vector<Object3d*> enemyPointers;
    // for (auto& t : testTargets_) {
    //     enemyPointers.push_back(t.get());
    // }
    // playerPtr_->SetEnemies(enemyPointers); //
    // Enemyクラスへの変更に伴い一旦無効化
  }

  // シリンダーの回転アニメーション
  cylinderUVOffset_ -= 0.0005f;
  if (cylinderUVOffset_ < -1.0f)
    cylinderUVOffset_ += 1.0f;
  Matrix4x4 uvTransform =
      MakeAffineMatrix(Vector3{1.0f, 1.0f, 1.0f}, Vector3{0.0f, 0.0f, 0.0f},
                       Vector3{cylinderUVOffset_, 0.0f, 0.0f});
  cylinderParticleGroup_->SetUVTransform(uvTransform);

  // シリンダー色変化
  cylinderColorHue_ += 0.001f;
  if (cylinderColorHue_ > 1.0f)
    cylinderColorHue_ -= 1.0f;
  float h = cylinderColorHue_ * 6.0f;
  int i = static_cast<int>(h);
  float f = h - i;
  float q = 1.0f - f;
  float t = f;
  Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
  switch (i % 6) {
  case 0:
    color = {1.0f, t, 0.0f, 1.0f};
    break;
  case 1:
    color = {q, 1.0f, 0.0f, 1.0f};
    break;
  case 2:
    color = {0.0f, 1.0f, t, 1.0f};
    break;
  case 3:
    color = {0.0f, q, 1.0f, 1.0f};
    break;
  case 4:
    color = {t, 0.0f, 1.0f, 1.0f};
    break;
  case 5:
    color = {1.0f, 0.0f, q, 1.0f};
    break;
  }
  cylinderParticleGroup_->SetMaterialColor(color);

  cylinderParticleGroup_->Update(activeCamera->GetViewMatrix(),
                                 activeCamera->GetProjectionMatrix());
  planeHitParticleGroup_->Update(activeCamera->GetViewMatrix(),
                                 activeCamera->GetProjectionMatrix());

  // 天球の更新
  if (skybox_) {
    skybox_->SetCamera(activeCamera);
    skybox_->Update();
  }

  // パーティクルの射出処理(GPU)
  ParticleManager::GetInstance()->Emit();

#ifdef USE_IMGUI
  auto *renderer = engine_->GetObject3dRenderer();

  //========================
  // PostProcess Settings UI
  //========================
  if (postProcess_) {
    elapsedTime_ += 1.0f / 60.0f;
    postProcess_->SetTime(elapsedTime_);
    uint32_t currentMaskSrvIndex =
        (useNoiseTextureType_ == 0) ? noise0TextureIndex_ : noise1TextureIndex_;
    postProcess_->SetMaskSrvIndex(currentMaskSrvIndex);

#ifdef USE_IMGUI
    ImGui::Begin("World Settings");

    // Draw the main PostProcess UI inside World Settings (without creating a
    // new window)
    postProcess_->DrawDebugUI("World Settings", false);

    // Draw the extra noise texture combo box if Dissolve is selected
    if (postProcess_->GetPostEffectType() == 6) { // Dissolve
      const char *noiseTypes[] = {"noise0.png", "noise1.png"};
      ImGui::Combo("Noise Texture", &useNoiseTextureType_, noiseTypes,
                   IM_ARRAYSIZE(noiseTypes));
    }

    ImGui::End();
#endif
  }

  //========================
  // Object3D Dissolve Test (Suzanne)
  //========================
  if (!testTargets_.empty() && testTargets_[0]) {
    ImGui::Begin("World Settings");
    if (ImGui::CollapsingHeader("Object3D Dissolve Test (Suzanne)",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Enable Dissolve", &suzanneEnableDissolve_);
      ImGui::SliderFloat("Threshold", &suzanneDissolveThreshold_, 0.0f, 1.0f);
      ImGui::SliderFloat("Edge Range", &suzanneDissolveEdgeRange_, 0.0f, 0.5f);
      ImGui::ColorEdit4("Edge Color", &suzanneDissolveEdgeColor_.x);
      ImGui::DragFloat2("Mask UV Offset", &suzanneMaskTransform_.x, 0.01f);

      if (ImGui::Button("Play Animation (or Press '1' Key)")) {
        suzanneEnableDissolve_ = true;
        suzanneDissolveThreshold_ = 0.0f;
        isPlayingSuzanneDissolve_ = true;
        isHologramMode_ = false;
      }

      ImGui::Separator();
      // ホログラム演出
      if (ImGui::Checkbox("Hologram / Energy Shield Mode", &isHologramMode_)) {
        if (isHologramMode_) {
          suzanneEnableDissolve_ = true;
          isPlayingSuzanneDissolve_ = false; // アニメーションを止める
          suzanneDissolveThreshold_ = 0.5f;  // 半分だけ表示
          suzanneDissolveEdgeRange_ = 0.1f;  // エッジを少し太めに
          suzanneDissolveEdgeColor_ = {0.0f, 1.0f, 1.0f, 1.0f}; // シアン色
          testTargets_[0]->SetMaskTexturePath(
              "resources/hex_noise.png"); // 六角形テクスチャを使用
        } else {
          suzanneEnableDissolve_ = false;
          suzanneDissolveThreshold_ = 0.0f;
          testTargets_[0]->SetMaskTexturePath(
              "resources/noise0.png"); // 元に戻す
        }
      }

      testTargets_[0]->SetEnableDissolve(suzanneEnableDissolve_);
      testTargets_[0]->SetDissolveThreshold(suzanneDissolveThreshold_);
      testTargets_[0]->SetDissolveEdgeRange(suzanneDissolveEdgeRange_);
      testTargets_[0]->SetMaskTransform(suzanneMaskTransform_);
      testTargets_[0]->SetDissolveEdgeColor(suzanneDissolveEdgeColor_);
    }
    ImGui::End();
  }

#endif // USE_IMGUI
}

void DebugScene::Draw() { Draw3D(); }

void DebugScene::Draw3D() {
  engine_->Begin3D();

  // ここから下で3DオブジェクトのDrawを呼ぶ

  if (skybox_) {
    engine_->GetSkyboxRenderer()->Begin();
    skybox_->Draw();

    // Skybox描画後に通常3Dへ戻す
    engine_->GetObject3dRenderer()->Begin();
  }

  object3d_->Draw();
  for (auto &target : testTargets_) {
    target->Draw();
  }
  animatedCube_->Draw();

  // 全アクターの3Dモデル部分だけをまとめて描く
  ActorManager::GetInstance()->Draw3D();

  // レベルデータのオブジェクト描画
  for (auto &obj : levelObjects_) {
    obj->Draw();
  }

  // 敵の描画
  for (auto &enemy : enemies_) {
    enemy->Draw();
  }

  // CSで計算済みの頂点を使って描画されるため、通常のBegin()のまま
  sneakWalk_->Draw();

  // スケルトンのデバッグ描画（最前面に表示するためZテスト無効）
  engine_->GetObject3dRenderer()->SetDepthEnable(false);

  // 関節間のLineを登録
  for (size_t i = 0; i < sneakWalk_->GetSkeleton().joints.size(); ++i) {
    const Joint &joint = sneakWalk_->GetSkeleton().joints[i];
    if (joint.parent) {
      const Joint &parentJoint =
          sneakWalk_->GetSkeleton().joints[*joint.parent];
      Vector3 start = {parentJoint.skeletonSpaceMatrix.m[3][0],
                       parentJoint.skeletonSpaceMatrix.m[3][1],
                       parentJoint.skeletonSpaceMatrix.m[3][2]};
      Vector3 end = {joint.skeletonSpaceMatrix.m[3][0],
                     joint.skeletonSpaceMatrix.m[3][1],
                     joint.skeletonSpaceMatrix.m[3][2]};
      engine_->GetLineRenderer()->DrawLine(start, end,
                                           {1.0f, 1.0f, 1.0f, 1.0f}); // 白い線
    }
  }

  //  関節のSphereを描画
  for (auto &jointObj : jointObjects_) {
    jointObj->Draw();
  }
  engine_->GetObject3dRenderer()->SetDepthEnable(true); // 元に戻す

  testParticleGroup_->Draw();
  clearParticleGroup_->Draw();
  hitParticleGroup_->Draw();
  cylinderParticleGroup_->Draw();
  planeHitParticleGroup_->Draw();

  // レールのデバッグ描画（Catmull-Rom曲線）
  if (waypoints_.size() >= 2) {
    const int divisions = 20; // 1区間あたりの分割数
    Vector3 prevPos = waypoints_[0];
    float maxT = static_cast<float>(waypoints_.size() - 1);

    // 曲線を緑色の線で描画
    for (float t = 0.0f; t <= maxT; t += 1.0f / divisions) {
      Vector3 currentPos = railCamera_->CalcPosition(t);
      engine_->GetLineRenderer()->DrawLine(prevPos, currentPos,
                                           {0.0f, 1.0f, 0.0f, 1.0f});
      prevPos = currentPos;
    }
  }

  //  Lineを描画 (他の描画のパイプラインを壊さないように最後に呼ぶ)
  if (const ICamera *camera =
          engine_->GetObject3dRenderer()->GetDefaultCamera()) {
    engine_->GetLineRenderer()->Render(camera->GetViewProjectionMatrix());
  }
}

void DebugScene::Draw2D() {
  // ここから下で2DオブジェクトのDrawを呼ぶ

  // まず通常描画で下地となる文字（上半分が見える部分）を描画
  engine_->GetSpriteRenderer()->Begin();
  sprites_[0]->Draw();

  // その上に、波エフェクト専用のシェーダーでもう一度描画（波の形にマスクされる）
  static float effectTime = 0.0f;
  effectTime += 1.0f / 60.0f;
  if (effectTime > 2.0f) {
    effectTime = 0.0f;
  }
  //  波打ちエフェクト (時間, タイプ, Yの境界線, 揺れ幅, 細かさ, スピード)
  sprites_[0]->SetUIEffectParams(effectTime, 1, 0.2f, 0.05f, 20.0f, 1.0f);

  engine_->GetSpriteRenderer()->BeginUIEffect();
  sprites_[0]->DrawUIEffect();

  // 通常描画に戻す
  engine_->GetSpriteRenderer()->Begin();
  // for (uint32_t i = 0; i < kSpriteCount_; ++i) {
  //   sprites_[i]->Draw();
  // }

  // アクターの2D部分の描画
  ActorManager::GetInstance()->Draw2D();
}

DebugScene::~DebugScene() = default;
