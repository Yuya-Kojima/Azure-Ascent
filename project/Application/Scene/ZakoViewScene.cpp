#include "ZakoViewScene.h"
#include "Actor/Enemy.h"
#include "Actor/EnemyLights.h"
#include "Camera/GameCamera.h"
#include "Debug/DebugCamera.h"
#include "Effect/EffectManager.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include "Math/MathUtil.h"
#include "Object3d/Object3d.h"
#include "Particle/ParticleManager.h"
#include "Render/Renderer/SkyBoxRenderer.h"
#include "Render/SkyBox/SkyBox.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/PostProcess.h"
#include "Scene/StageLighting.h"
#include "../../externals/nlohmann/json.hpp"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numbers>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// プレハブの名前
const char *const kEyePrefab = "ZakoEye";
const char *const kArrowPrefab = "ZakoArrow";

// ゲームのカメラ（RailCamera）と同じ縦の画角
const float kFovY = 45.0f * (std::numbers::pi_v<float> / 180.0f);
// 画面の縦のピクセル数
const float kScreenHeight = 720.0f;
// 敵の大きさ（いちばん長い辺）
const float kEnemySize = 6.0f;
// 画面での大きさのスライダーの範囲（ピクセル）
const float kMinPixels = 15.0f;
const float kMaxPixels = 150.0f;

// A と B を並べるときの、中心から左右へのずれ
const float kSideBySideOffset = 8.0f;
// A の輪の半径と体数
const float kRingRadius = 15.0f;
const int kRingCount = 8;
// B の列の間隔と体数
const float kRowSpacing = 12.0f;
const int kRowCount = 5;

// 背景の板: 敵の後ろの距離・大きさ・明るさの目安（区間ごとの画面の平均の明るさに近い色）
const float kBackdropBehind = 150.0f;
const float kBackdropSize = 4000.0f;
const Vector3 kBackdropSection1 = {0.19f, 0.21f, 0.24f};
const Vector3 kBackdropSection2 = {0.12f, 0.13f, 0.155f};
const Vector3 kBackdropSection3 = {0.085f, 0.094f, 0.11f};

// 向きの角度
const float kPi = std::numbers::pi_v<float>;
// B を回し続けるときの回る速さ（ラジアン/秒）と、少し上から見る傾き
const float kSpinSpeed = 0.6f;
const float kSpinPitch = -0.35f;
// B を斜めから見るときの傾きと向き
const float kAngledPitch = -0.5f;
const float kAngledYaw = 0.75f * std::numbers::pi_v<float>;

// 被弾の点滅をくり返す間隔（フレーム）
const int kHitFlashInterval = 60;

// 1フレームの時間（秒）
const float kFrameTime = 1.0f / 60.0f;

// 被弾の色
const Vector4 kRedHitFlash = {1.0f, 0.0f, 0.0f, 1.0f};
const Vector4 kWhiteHitFlash = {1.0f, 1.0f, 1.0f, 1.0f};

std::string GetPrefabFilePath(const std::string &prefabName) {
  return "resources/prefabs/" + prefabName + ".prefab";
}

bool LoadJson(const std::string &path, nlohmann::json &root) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }
  try {
    file >> root;
  } catch (const nlohmann::json::exception &) {
    return false;
  }
  return root.is_object();
}

Vector3 ReadVector3(const nlohmann::json &value) {
  return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

nlohmann::json ToJson(const Vector3 &v) { return {v.x, v.y, v.z}; }
} // namespace

ZakoViewScene::ZakoViewScene() = default;

ZakoViewScene::~ZakoViewScene() = default;

void ZakoViewScene::Initialize(EngineBase *engine) {

  // 基底クラスの初期化（PostProcessの生成など）
  BaseScene::Initialize(engine);

  // 参照をコピー
  engine_ = engine;

  // 前のシーンの弾や破片を消す
  ActorManager::GetInstance()->Clear();

  // ゲームと同じ画角のカメラ（原点から +Z を見る）
  camera_ = std::make_unique<GameCamera>();
  camera_->SetFovY(kFovY);
  camera_->SetTranslate({0.0f, 0.0f, 0.0f});
  camera_->SetRotate({0.0f, 0.0f, 0.0f});
  camera_->Update();

  debugCamera_ = std::make_unique<DebugCamera>();
  debugCamera_->Initialize({0.0f, 10.0f, -20.0f});

  // 空（GamePlayScene と同じ作り方）
  skybox_ = std::make_unique<Skybox>();
  skybox_->Initialize(engine_->GetSkyboxRenderer());
  skybox_->SetTexture("resources/Skybox/Skybox.dds");
  skybox_->SetScale({100.0f, 100.0f, 100.0f});
  skybox_->SetProceduralSky(true);

  lighting_ = std::make_unique<StageLighting>();
  lighting_->Initialize();

  // 背景の板
  backdrop_ = std::make_unique<Object3d>();
  backdrop_->Initialize(engine_->GetObject3dRenderer());
  backdrop_->SetModel("__builtin_box");
  backdrop_->SetEnableLighting(false);
  backdrop_->SetFlatUnlit(true);
  backdrop_->SetScale({kBackdropSize, kBackdropSize, 1.0f});
  backdropColor_ = kBackdropSection1;

  LoadLook();
  Respawn();
}

void ZakoViewScene::Finalize() {
  placed_.clear();
  ActorManager::GetInstance()->Clear();
  // 平行光源は全シーン共通なので、このシーンで足した環境光と逆光の縁を外しておく
  StageLighting::ResetSharedLight(engine_->GetObject3dRenderer());
}

float ZakoViewScene::GetDistance() const {
  // 距離 d で縦の画面に映る高さは 2 x d x tan(画角 / 2)。
  // そのうち敵の大きさが占める割合 = ピクセル数 / 画面の縦のピクセル数 から d を求める
  return kEnemySize * kScreenHeight /
         (2.0f * std::tan(kFovY * 0.5f) * targetPixels_);
}

void ZakoViewScene::Respawn() {
  placed_.clear();

  auto add = [this](const char *prefabName, const Vector3 &offset,
                    bool isArrow) {
    Placed placed;
    placed.enemy =
        PrefabManager::GetInstance()->InstantiateEnemy(prefabName, Transform{});
    placed.offset = offset;
    placed.isArrow = isArrow;
    placed_.push_back(std::move(placed));
  };

  switch (layout_) {
  case Layout::kSideBySide:
    add(kEyePrefab, {-kSideBySideOffset, 0.0f, 0.0f}, false);
    add(kArrowPrefab, {kSideBySideOffset, 0.0f, 0.0f}, true);
    break;
  case Layout::kEyeRing:
    for (int i = 0; i < kRingCount; ++i) {
      const float angle = 2.0f * kPi * static_cast<float>(i) /
                          static_cast<float>(kRingCount);
      add(kEyePrefab,
          {kRingRadius * std::cos(angle), kRingRadius * std::sin(angle), 0.0f},
          false);
    }
    break;
  case Layout::kArrowRow:
    for (int i = 0; i < kRowCount; ++i) {
      const float x =
          (static_cast<float>(i) - static_cast<float>(kRowCount - 1) * 0.5f) *
          kRowSpacing;
      add(kArrowPrefab, {x, 0.0f, 0.0f}, true);
    }
    break;
  }

  ApplyToEnemies();
}

void ZakoViewScene::ApplyToEnemies() {
  const float distance = GetDistance();
  // B の列を流した量は、列の長さで折り返す（右端から出直す）
  const float rowLength = kRowSpacing * static_cast<float>(kRowCount);

  for (Placed &placed : placed_) {
    Enemy *enemy = placed.enemy.get();

    // 位置
    Vector3 position = {placed.offset.x, placed.offset.y, distance};
    if (layout_ == Layout::kArrowRow) {
      position.x = std::fmod(placed.offset.x - rowShift_ + rowLength * 1.5f,
                             rowLength) -
                   rowLength * 0.5f;
    }
    enemy->GetTransform().translate = position;

    // 向き（前 +Z を回す。A は目をカメラへ向ける）
    Vector3 rotate = {0.0f, kPi, 0.0f};
    if (placed.isArrow) {
      if (layout_ == Layout::kArrowRow) {
        rotate = {0.0f, -kPi * 0.5f, 0.0f}; // 鼻先を左（-X）へ
      } else {
        switch (arrowFacing_) {
        case ArrowFacing::kFront:
          rotate = {0.0f, kPi, 0.0f};
          break;
        case ArrowFacing::kSide:
          rotate = {0.0f, kPi * 0.5f, 0.0f};
          break;
        case ArrowFacing::kTop:
          rotate = {-kPi * 0.5f, 0.0f, 0.0f}; // 背中（+Y）をカメラ（-Z）へ
          break;
        case ArrowFacing::kAngled:
          rotate = {kAngledPitch, kAngledYaw, 0.0f};
          break;
        case ArrowFacing::kSpin:
          rotate = {kSpinPitch, spinAngle_, 0.0f};
          break;
        }
      }
    }
    enemy->GetTransform().rotate = rotate;

    // 見た目
    enemy->SetBaseColor({look_.bodyColor.x, look_.bodyColor.y,
                         look_.bodyColor.z, 1.0f});
    enemy->SetRimLight(look_.rimColor, look_.rimIntensity, look_.rimPower);
    enemy->SetHitFlashColor(look_.whiteHitFlash ? kWhiteHitFlash
                                                : kRedHitFlash);
    if (EnemyLights *lights = enemy->GetLights()) {
      lights->SetColor(look_.lightColor, look_.lightIntensity);
    }
  }
}

void ZakoViewScene::Update() {

  // カメラ
  const ICamera *activeCamera = camera_.get();
  if (useDebugCamera_) {
    debugCamera_->Update(*engine_->GetInputManager());
    activeCamera = debugCamera_->GetCamera();
  } else {
    camera_->Update();
  }
  engine_->GetObject3dRenderer()->SetDefaultCamera(activeCamera);

#ifdef USE_IMGUI
  DrawDebugUI();
#endif // USE_IMGUI

  // 空・光・霧・ポストエフェクト
  lighting_->Apply(skybox_.get(), engine_->GetObject3dRenderer(),
                   postProcess_.get(), skyTime_);
  skybox_->SetCamera(activeCamera);
  skybox_->Update();

  // 背景の板
  backdrop_->SetTranslation({0.0f, 0.0f, GetDistance() + kBackdropBehind});
  backdrop_->SetColor({backdropColor_.x, backdropColor_.y, backdropColor_.z,
                       1.0f});
  backdrop_->Update();

  // 動き
  spinAngle_ += kSpinSpeed * kFrameTime;
  rowShift_ += rowSpeed_ * kFrameTime;

  // 被弾の点滅のくり返し（体力は減らさない）
  if (repeatHitFlash_ && ++hitFlashFrame_ >= kHitFlashInterval) {
    hitFlashFrame_ = 0;
    for (Placed &placed : placed_) {
      placed.enemy->TakeDamage(0);
    }
  }

  // 敵
  ApplyToEnemies();
  for (Placed &placed : placed_) {
    placed.enemy->Update();
  }
  std::erase_if(placed_,
                [](const Placed &placed) { return placed.enemy->IsDead(); });

  // 撃破の破片とエフェクト
  ActorManager::GetInstance()->Update();
  EffectManager::GetInstance()->Update(activeCamera);
}

void ZakoViewScene::DrawDebugUI() {
#ifdef USE_IMGUI
  ImGui::Begin("Zako View");

  ImGui::SeparatorText("並べ方");
  int layout = static_cast<int>(layout_);
  bool layoutChanged = false;
  layoutChanged |= ImGui::RadioButton("A と B", &layout,
                                      static_cast<int>(Layout::kSideBySide));
  ImGui::SameLine();
  layoutChanged |= ImGui::RadioButton("A の 8 体の輪", &layout,
                                      static_cast<int>(Layout::kEyeRing));
  ImGui::SameLine();
  layoutChanged |= ImGui::RadioButton("B の列（左へ流れる）", &layout,
                                      static_cast<int>(Layout::kArrowRow));
  if (layoutChanged) {
    layout_ = static_cast<Layout>(layout);
    Respawn();
  }
  if (ImGui::Button("35px")) {
    targetPixels_ = 35.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("65px")) {
    targetPixels_ = 65.0f;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(160.0f);
  ImGui::SliderFloat("画面での大きさ（px）", &targetPixels_, kMinPixels,
                     kMaxPixels, "%.0f");
  ImGui::Text("距離 %.0f m", GetDistance());

  if (layout_ == Layout::kSideBySide) {
    int facing = static_cast<int>(arrowFacing_);
    ImGui::Text("B の向き");
    ImGui::RadioButton("正面", &facing, static_cast<int>(ArrowFacing::kFront));
    ImGui::SameLine();
    ImGui::RadioButton("真横", &facing, static_cast<int>(ArrowFacing::kSide));
    ImGui::SameLine();
    ImGui::RadioButton("真上", &facing, static_cast<int>(ArrowFacing::kTop));
    ImGui::SameLine();
    ImGui::RadioButton("斜め", &facing, static_cast<int>(ArrowFacing::kAngled));
    ImGui::SameLine();
    ImGui::RadioButton("回す", &facing, static_cast<int>(ArrowFacing::kSpin));
    arrowFacing_ = static_cast<ArrowFacing>(facing);
  }
  if (layout_ == Layout::kArrowRow) {
    ImGui::SliderFloat("流れる速さ（m/秒）", &rowSpeed_, 0.0f, 60.0f, "%.0f");
  }

  ImGui::SeparatorText("背景");
  ImGui::Checkbox("背景の板を置く", &showBackdrop_);
  if (ImGui::Button("区間1 の明るさ")) {
    backdropColor_ = kBackdropSection1;
  }
  ImGui::SameLine();
  if (ImGui::Button("区間2")) {
    backdropColor_ = kBackdropSection2;
  }
  ImGui::SameLine();
  if (ImGui::Button("区間3")) {
    backdropColor_ = kBackdropSection3;
  }
  ImGui::ColorEdit3("板の色", &backdropColor_.x, ImGuiColorEditFlags_Float);
  ImGui::DragFloat("空の時刻", &skyTime_, 0.5f, 0.0f, 600.0f, "%.0f");

  ImGui::SeparatorText("見た目（A と B で共通）");
  ImGui::ColorEdit3("体の色", &look_.bodyColor.x, ImGuiColorEditFlags_Float);
  ImGui::ColorEdit3("ふちの光の色", &look_.rimColor.x,
                    ImGuiColorEditFlags_Float);
  ImGui::SliderFloat("ふちの光の強さ", &look_.rimIntensity, 0.0f, 4.0f);
  ImGui::SliderFloat("ふちの光の鋭さ", &look_.rimPower, 0.5f, 8.0f);
  ImGui::ColorEdit3("光の色", &look_.lightColor.x, ImGuiColorEditFlags_Float);
  ImGui::SliderFloat("光の強さ", &look_.lightIntensity, 0.0f, 10.0f);
  ImGui::Checkbox("被弾の色を白にする（オフで赤）", &look_.whiteHitFlash);
  if (ImGui::Button("ZakoEye・ZakoArrow に保存")) {
    SaveLook();
  }
  ImGui::SameLine();
  if (ImGui::Button("プレハブから読み直す")) {
    LoadLook();
    saveMessage_.clear();
  }
  if (!saveMessage_.empty()) {
    ImGui::TextUnformatted(saveMessage_.c_str());
  }

  ImGui::SeparatorText("撃つ");
  if (ImGui::Button("被弾の点滅（体力は減らさない）")) {
    for (Placed &placed : placed_) {
      placed.enemy->TakeDamage(0);
    }
  }
  ImGui::SameLine();
  ImGui::Checkbox("1 秒ごとにくり返す", &repeatHitFlash_);
  if (ImGui::Button("全部を撃破")) {
    for (Placed &placed : placed_) {
      placed.enemy->TakeDamage(999);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("出し直す")) {
    Respawn();
  }

  ImGui::SeparatorText("カメラ");
  ImGui::Checkbox("自由カメラ（右ドラッグで回転・WASD で移動。画角はゲームと違う）",
                  &useDebugCamera_);

  ImGui::End();
#endif // USE_IMGUI
}

void ZakoViewScene::LoadLook() {
  nlohmann::json root;
  if (!LoadJson(GetPrefabFilePath(kEyePrefab), root)) {
    return;
  }
  if (root.contains("color")) {
    look_.bodyColor = ReadVector3(root["color"]);
  }
  if (root.contains("rimLight")) {
    const nlohmann::json &rim = root["rimLight"];
    if (rim.contains("color")) {
      look_.rimColor = ReadVector3(rim["color"]);
    }
    look_.rimIntensity = rim.value("intensity", look_.rimIntensity);
    look_.rimPower = rim.value("power", look_.rimPower);
  }
  if (root.contains("lights") && root["lights"].is_array() &&
      !root["lights"].empty()) {
    const nlohmann::json &light = root["lights"][0];
    if (light.contains("color")) {
      look_.lightColor = ReadVector3(light["color"]);
    }
    look_.lightIntensity = light.value("intensity", look_.lightIntensity);
  }
  // 被弾の色は、白が書いてあれば白、なければ赤（プレハブにない敵と同じ）
  look_.whiteHitFlash = false;
  if (root.contains("hitFlashColor")) {
    const Vector3 flash = ReadVector3(root["hitFlashColor"]);
    look_.whiteHitFlash = (flash.y > 0.5f && flash.z > 0.5f);
  }
}

void ZakoViewScene::SaveLook() {
  saveMessage_.clear();
  for (const char *prefabName : {kEyePrefab, kArrowPrefab}) {
    const std::string path = GetPrefabFilePath(prefabName);
    nlohmann::json root;
    if (!LoadJson(path, root)) {
      saveMessage_ += std::string(prefabName) + " を読めませんでした。";
      continue;
    }
    root["color"] = ToJson(look_.bodyColor);
    root["rimLight"]["color"] = ToJson(look_.rimColor);
    root["rimLight"]["intensity"] = look_.rimIntensity;
    root["rimLight"]["power"] = look_.rimPower;
    if (root.contains("lights") && root["lights"].is_array()) {
      for (nlohmann::json &light : root["lights"]) {
        light["color"] = ToJson(look_.lightColor);
        light["intensity"] = look_.lightIntensity;
      }
    }
    if (look_.whiteHitFlash) {
      root["hitFlashColor"] = ToJson({kWhiteHitFlash.x, kWhiteHitFlash.y,
                                      kWhiteHitFlash.z});
    } else {
      root.erase("hitFlashColor");
    }
    std::ofstream file(path);
    if (!file.is_open()) {
      saveMessage_ += std::string(prefabName) + " に書けませんでした。";
      continue;
    }
    file << std::setw(4) << root << std::endl;
  }
  if (saveMessage_.empty()) {
    saveMessage_ = "保存しました（ゲームでは次に敵を出したときから反映）";
  }
}

void ZakoViewScene::Draw() { Draw3D(); }

void ZakoViewScene::Draw3D() {
  engine_->Begin3D();

  // 空
  engine_->GetSkyboxRenderer()->Begin();
  skybox_->Draw();
  engine_->GetObject3dRenderer()->Begin();

  if (showBackdrop_) {
    backdrop_->Draw();
  }
  for (Placed &placed : placed_) {
    placed.enemy->Draw3D();
  }

  // 撃破の破片
  ActorManager::GetInstance()->Draw3D();

  // 撃破のエフェクト（ルートシグネチャが変わるため、3Dモデル描画後に）
  ParticleManager::GetInstance()->Update();
  ParticleManager::GetInstance()->Emit();
  EffectManager::GetInstance()->Draw();
}

void ZakoViewScene::Draw2D() {}
