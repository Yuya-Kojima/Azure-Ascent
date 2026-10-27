#include "StageSelectReturn.h"
#include "../../externals/nlohmann/json.hpp"
#include "Input/Input.h"
#include "Input/InputKeyState.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
const char *const kConfigDirectory = "resources/config";
const char *const kConfigPath = "resources/config/GameOverReturnConfig.json";
// カメラの節目を左右入れ替えるかを書くキー
const char *const kMirrorKey = "camera.mirrored";
// 1フレームの時間（秒）
const float kFrameSec = 1.0f / 60.0f;
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
// UV は画像全体をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
// 数字の画像（0〜9 と % を横に並べたもの）の1文字の大きさ（px）と、% の位置
const Vector2 kDigitCellSize = {48.0f, 72.0f};
const int kPercentCell = 10;
// 数字を描く大きさ（画像に対する倍率）
const float kDigitScale = 0.75f;
// 「到達度」の画像を描く大きさ（画像に対する倍率）
const float kLabelScale = 0.8f;
// 選んでいる方の印の高さ（px）
const float kMarkerHeight = 44.0f;
// 到達度のバーの下地の不透明度
const float kBarBackAlpha = 0.45f;

/// <summary>
/// 時刻 time が start から sec かけて進む割合（0〜1）
/// </summary>
float Progress(float time, float start, float sec) {
  if (sec <= 0.0f) {
    return time >= start ? 1.0f : 0.0f;
  }
  return std::clamp((time - start) / sec, 0.0f, 1.0f);
}

/// <summary>
/// 羽ばたきの状態を割合 t で混ぜる
/// </summary>
DragonWingAnimator::State LerpWing(const DragonWingAnimator::State &a,
                                   const DragonWingAnimator::State &b,
                                   float t) {
  DragonWingAnimator::State result;
  result.strength = Lerp(a.strength, b.strength, t);
  result.frequency = Lerp(a.frequency, b.frequency, t);
  result.fold = Lerp(a.fold, b.fold, t);
  result.asymmetry = Lerp(a.asymmetry, b.asymmetry, t);
  result.pose = Lerp(a.pose, b.pose, t);
  result.hover = Lerp(a.hover, b.hover, t);
  return result;
}

/// <summary>
/// 決定の入力（エンター・スペース・パッドの A）
/// </summary>
bool IsDecidePressed(Input *input) {
  return input->IsTriggerKey(DIK_RETURN) || input->IsTriggerKey(DIK_SPACE) ||
         input->IsPadTrigger(PadButton::A);
}

/// <summary>
/// 前の区間の傾き a と次の区間の傾き b から、節目での傾きを決める
/// 向きが変わる節目では 0、同じ向きなら調和平均（どちらの傾きの 2 倍も超えない）
/// </summary>
float KeySlope(float a, float b) {
  if (a * b <= 0.0f) {
    return 0.0f;
  }
  return 2.0f * a * b / (a + b);
}

/// <summary>
/// 3次エルミート曲線（両端の値 p0・p1、両端の傾き m0・m1、区間の長さ span、区間の中の割合 t）
/// </summary>
float Hermite(float p0, float p1, float m0, float m1, float span, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 +
         (t3 - 2.0f * t2 + t) * span * m0 + (-2.0f * t3 + 3.0f * t2) * p1 +
         (t3 - t2) * span * m1;
}

// 区間の長さの下限（秒）。節目の時刻が同じでも 0 で割らないため
const float kMinKeySpan = 0.01f;
} // namespace

StageSelectReturn::StageSelectReturn() = default;

StageSelectReturn::~StageSelectReturn() = default;

template <class Visitor>
void StageSelectReturn::VisitConfigValues(Visitor &&visit) {
  auto visitVector3 = [&](const std::string &key, Vector3 &v) {
    visit(key + ".x", v.x);
    visit(key + ".y", v.y);
    visit(key + ".z", v.z);
  };
  auto visitWing = [&](const std::string &key, DragonWingAnimator::State &w) {
    visit(key + ".strength", w.strength);
    visit(key + ".frequency", w.frequency);
    visit(key + ".fold", w.fold);
    visit(key + ".asymmetry", w.asymmetry);
  };
  visit("time.lookUpTime", lookUpTime_);
  visit("time.lookUpSec", lookUpSec_);
  visit("time.menuTime", menuTime_);
  visit("time.menuFadeSec", menuFadeSec_);
  visit("time.barFillSec", barFillSec_);
  visit("time.leaveSec", leaveSec_);
  visit("time.stayCameraSec", stayCameraSec_);
  visit("time.glowReturnTime", glowReturnTime_);
  visit("time.glowReturnSec", glowReturnSec_);

  visit("dragon.lieDepth", lieDepth_);
  visit("dragon.chestRaise", chestRaise_);
  visit("dragon.wingMotion", wingMotion_);
  visit("dragon.stirSec", stirSec_);
  visitWing("dragon.lieWing", lieWing_);
  visitWing("dragon.stirWing", stirWing_);
  visitWing("dragon.standWing", standWing_);
  visit("dragon.returnPattern", returnPattern_);

  visit("camera.descentStartSpeed", descentStartSpeed_);
  for (size_t i = 0; i < cameraKeys_.size(); ++i) {
    const std::string key = "camera.key" + std::to_string(i);
    visit(key + ".time", cameraKeys_[i].time);
    visitVector3(key + ".camera", cameraKeys_[i].cameraOffset);
    visitVector3(key + ".look", cameraKeys_[i].lookOffset);
  }
}

void StageSelectReturn::LoadConfig() {
  std::ifstream file(kConfigPath);
  if (!file.is_open()) {
    return;
  }
  nlohmann::json root;
  try {
    file >> root;
  } catch (const nlohmann::json::exception &) {
    return;
  }
  // ファイルにないキーは今の値のまま
  VisitConfigValues([&](const std::string &key, float &value) {
    if (root.contains(key) && root[key].is_number()) {
      value = root[key].get<float>();
    }
  });
  if (root.contains(kMirrorKey) && root[kMirrorKey].is_boolean()) {
    isCameraMirrored_ = root[kMirrorKey].get<bool>();
  }
}

void StageSelectReturn::SaveConfig() {
  nlohmann::json root;
  VisitConfigValues(
      [&](const std::string &key, float &value) { root[key] = value; });
  root[kMirrorKey] = isCameraMirrored_;
  if (!std::filesystem::exists(kConfigDirectory)) {
    std::filesystem::create_directories(kConfigDirectory);
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

void StageSelectReturn::Initialize(SpriteRenderer *spriteRenderer) {
  LoadConfig();

  // 左端・縦の中心を位置に合わせ、画像の大きさに scale を掛けたスプライトを作る
  auto create = [spriteRenderer](const std::string &path, float scale) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint({0.0f, 0.5f});
    const Vector2 size = sprite->GetSize();
    sprite->SetSize({size.x * scale, size.y * scale});
    return sprite;
  };
  label_ = create("resources/UI/GameOver/progress_label.png", kLabelScale);
  retry_ = create("resources/UI/GameOver/retry.png", choiceScale_);
  stay_ = create("resources/UI/GameOver/stay.png", choiceScale_);
  barBack_ = create("resources/white1x1.png", 1.0f);
  barFill_ = create("resources/white1x1.png", 1.0f);
  marker_ = create("resources/white1x1.png", 1.0f);
  for (auto &digit : digits_) {
    digit = create("resources/UI/GameOver/digits.png", 1.0f);
    digit->SetTextureSize(kDigitCellSize);
    digit->SetSize(
        {kDigitCellSize.x * kDigitScale, kDigitCellSize.y * kDigitScale});
  }
}

void StageSelectReturn::Start(const Vector3 &dragonPos,
                              const Vector3 &dragonRot,
                              const Vector3 &baseCameraPos,
                              const Vector3 &baseCameraRot, float progress) {
  isActive_ = true;
  phase_ = Phase::Wake;
  timer_ = 0.0f;
  phaseTimer_ = 0.0f;
  pendingChoice_ = Choice::None;
  focused_ = Choice::Retry;
  progress_ = std::clamp(progress, 0.0f, 1.0f);
  uiAlpha_ = 0.0f;

  basePos_ = dragonPos;
  baseRot_ = dragonRot;
  baseCameraPos_ = baseCameraPos;
  baseCameraRot_ = baseCameraRot;

  // ドラゴンの向き（Y 軸まわりの角度）から、水平な前と右を求める
  forward_ = {std::sin(dragonRot.y), 0.0f, std::cos(dragonRot.y)};
  right_ = Normalize(Cross(kWorldUp, forward_));

  UpdateWake();
  UpdateGlow();
  UpdateCamera();
  UpdateSprites();
}

void StageSelectReturn::Update(Input *input) {
  if (!isActive_) {
    return;
  }
  // 止めている間は、つまみで決めた時刻の姿を映し、操作を受けない
  if (isPaused_) {
    input = nullptr;
    if (phase_ == Phase::Menu && timer_ < menuTime_) {
      phase_ = Phase::Wake;
    }
    phaseTimer_ = timer_ - menuTime_;
  } else {
    timer_ += kFrameSec;
    phaseTimer_ += kFrameSec;
  }

  switch (phase_) {
  case Phase::Wake:
    // 決定で、UI が出る所まで飛ばす
    if (input && timer_ >= skipLockSec_ && IsDecidePressed(input)) {
      timer_ = menuTime_;
    }
    UpdateWake();
    UpdateGlow();
    UpdateCamera();
    if (timer_ >= menuTime_) {
      phase_ = Phase::Menu;
      phaseTimer_ = timer_ - menuTime_;
    }
    break;
  case Phase::Menu:
    UpdateWake();
    UpdateGlow();
    UpdateCamera();
    UpdateMenu(input);
    break;
  case Phase::Retry:
  case Phase::Stay:
    UpdateLeave();
    break;
  }

  UpdateSprites();
}

StageSelectReturn::Choice StageSelectReturn::ConsumeChoice() {
  Choice choice = pendingChoice_;
  pendingChoice_ = Choice::None;
  return choice;
}

bool StageSelectReturn::IsDrivingDragon() const {
  return isActive_ && phase_ != Phase::Retry;
}

bool StageSelectReturn::IsMenuShown() const { return phase_ != Phase::Wake; }

Vector3 StageSelectReturn::ToWorld(const Vector3 &offset) const {
  return basePos_ + right_ * offset.x + kWorldUp * offset.y +
         forward_ * offset.z;
}

void StageSelectReturn::UpdateWake() {
  //===========================
  // 伏せて休む → 首を持ち上げて空を見上げる → 見上げたまま止まり、翼が少し動く
  //===========================
  const float lookUp =
      EaseInOutCubic(Progress(timer_, lookUpTime_, lookUpSec_));
  const float lookUpEnd = lookUpTime_ + lookUpSec_;
  const float stir = EaseInOutCubic(Progress(timer_, lookUpEnd, stirSec_));

  // 伏せの姿勢は腹が浮くので、その分沈める
  dragonPos_ = basePos_ - kWorldUp * lieDepth_;
  dragonRot_ = baseRot_;

  gesture_ = {};
  gesture_.lie = 1.0f;
  gesture_.lieChest = chestRaise_ * lookUp;
  gesture_.lookUp = lookUp;

  wingState_ = LerpWing(lieWing_, stirWing_, wingMotion_ * stir);
}

void StageSelectReturn::UpdateGlow() {
  //===========================
  // 竜の力の光：拠点に落ちてきたときは消えていて、見上げたあとかすかに戻る。傷は開いたまま暗い
  //===========================
  const float glowReturn =
      EaseInOutCubic(Progress(timer_, glowReturnTime_, glowReturnSec_));
  glow_.pattern = returnPattern_ * glowReturn;
  glow_.woundOpen = 1.0f;
  glow_.woundGlow = 0.0f;
}

Vector3 StageSelectReturn::SampleCameraKeys(Vector3 CameraKey::*offset) const {
  //===========================
  // 節目を通る一本の曲線（右・上・前それぞれ3次エルミート曲線でつなぐ）
  // 途中の節目では止まらず、最初の節目は速く出て、最後の節目で止まる
  //===========================
  const size_t last = cameraKeys_.size() - 1;
  if (timer_ <= cameraKeys_[0].time) {
    return cameraKeys_[0].*offset;
  }
  if (timer_ >= cameraKeys_[last].time) {
    return cameraKeys_[last].*offset;
  }
  size_t seg = 0;
  while (timer_ >= cameraKeys_[seg + 1].time) {
    ++seg;
  }

  // 区間 i の長さ（秒）
  auto span = [this](size_t i) {
    return (std::max)(cameraKeys_[i + 1].time - cameraKeys_[i].time,
                      kMinKeySpan);
  };
  // 区間 i の平均の傾き（1秒あたりの変化）
  auto slope = [&](size_t i, float Vector3::*axis) {
    return ((cameraKeys_[i + 1].*offset).*axis -
            (cameraKeys_[i].*offset).*axis) /
           span(i);
  };
  // 節目 k での傾き
  auto keySlope = [&](size_t k, float Vector3::*axis) {
    if (k == 0) {
      return slope(0, axis) * descentStartSpeed_;
    }
    if (k == last) {
      return 0.0f;
    }
    return KeySlope(slope(k - 1, axis), slope(k, axis));
  };

  const float segSpan = span(seg);
  const float t = std::clamp((timer_ - cameraKeys_[seg].time) / segSpan,
                             0.0f, 1.0f);
  Vector3 result{};
  for (float Vector3::*axis : {&Vector3::x, &Vector3::y, &Vector3::z}) {
    result.*axis = Hermite((cameraKeys_[seg].*offset).*axis,
                           (cameraKeys_[seg + 1].*offset).*axis,
                           keySlope(seg, axis), keySlope(seg + 1, axis),
                           segSpan, t);
  }
  return result;
}

void StageSelectReturn::UpdateCamera() {
  Vector3 cameraOffset = SampleCameraKeys(&CameraKey::cameraOffset);
  Vector3 lookOffset = SampleCameraKeys(&CameraKey::lookOffset);
  if (isCameraMirrored_) {
    cameraOffset.x = -cameraOffset.x;
    lookOffset.x = -lookOffset.x;
  }
  cameraPos_ = ToWorld(cameraOffset);
  cameraRot_ = LookRotation(cameraPos_, ToWorld(lookOffset));
}

void StageSelectReturn::UpdateMenu(Input *input) {
  uiAlpha_ = Progress(phaseTimer_, 0.0f, menuFadeSec_);

  // UI が現れきってから操作を受ける
  if (!input || uiAlpha_ < 1.0f) {
    return;
  }
  const bool isUp = input->IsTriggerKey(DIK_UP) || input->IsTriggerKey(DIK_W) ||
                    input->IsPadTrigger(PadButton::DPadUp);
  const bool isDown = input->IsTriggerKey(DIK_DOWN) ||
                      input->IsTriggerKey(DIK_S) ||
                      input->IsPadTrigger(PadButton::DPadDown);
  if (isUp || isDown) {
    focused_ = (focused_ == Choice::Retry) ? Choice::Stay : Choice::Retry;
  }

  if (IsDecidePressed(input)) {
    pendingChoice_ = focused_;
    phase_ = (focused_ == Choice::Retry) ? Phase::Retry : Phase::Stay;
    phaseTimer_ = 0.0f;
    leaveGesture_ = gesture_;
    leaveWing_ = wingState_;
    leaveCameraPos_ = cameraPos_;
    leaveCameraRot_ = cameraRot_;
  }
}

void StageSelectReturn::UpdateLeave() {
  //===========================
  // UI を消し、伏せて見上げる姿勢から立ち姿勢へ戻す。拠点に残るときは、カメラをいつもの位置へ戻す
  //===========================
  const float fade = Progress(phaseTimer_, 0.0f, leaveSec_);
  uiAlpha_ = 1.0f - fade;
  const float remain = 1.0f - EaseInOutQuad(fade);
  gesture_ = {};
  gesture_.lie = leaveGesture_.lie * remain;
  gesture_.lieChest = leaveGesture_.lieChest;
  gesture_.lookUp = leaveGesture_.lookUp * remain;
  dragonPos_ = basePos_ - kWorldUp * (lieDepth_ * gesture_.lie);
  dragonRot_ = baseRot_;
  wingState_ = LerpWing(leaveWing_, standWing_, 1.0f - remain);

  float endSec = leaveSec_;
  if (phase_ == Phase::Stay) {
    const float back =
        EaseInOutCubic(Progress(phaseTimer_, 0.0f, stayCameraSec_));
    cameraPos_ = Lerp(leaveCameraPos_, baseCameraPos_, back);
    cameraRot_ = {LerpAngle(leaveCameraRot_.x, baseCameraRot_.x, back),
                  LerpAngle(leaveCameraRot_.y, baseCameraRot_.y, back),
                  LerpAngle(leaveCameraRot_.z, baseCameraRot_.z, back)};
    endSec = (std::max)(leaveSec_, stayCameraSec_);
  }
  if (phaseTimer_ >= endSec) {
    isActive_ = false;
  }
}

void StageSelectReturn::UpdateSprites() {
  // 到達度：UI が出てから、バーと数字を 0 から伸ばす
  const float fill =
      EaseOutCubic(Progress(timer_, menuTime_, barFillSec_));
  const float shown = progress_ * fill;
  const int percent = static_cast<int>(std::lround(shown * 100.0f));
  const std::string text = std::to_string(percent) + "%";
  digitCount_ = static_cast<int>((std::min)(text.size(), digits_.size()));

  const Vector4 white = {1.0f, 1.0f, 1.0f, uiAlpha_};
  for (int i = 0; i < digitCount_; ++i) {
    const char c = text[i];
    const int cell = (c == '%') ? kPercentCell : (c - '0');
    Sprite &digit = *digits_[i];
    digit.SetTextureLeftTop({kDigitCellSize.x * cell, 0.0f});
    digit.SetPosition({digitsLeft_ + digitAdvance_ * i, labelY_});
    digit.SetColor(white);
    digit.Update(kUvTransform);
  }

  label_->SetPosition({menuLeft_, labelY_});
  label_->SetColor(white);
  label_->Update(kUvTransform);

  barBack_->SetPosition({menuLeft_, barY_});
  barBack_->SetSize({barWidth_, barHeight_});
  barBack_->SetColor({0.0f, 0.0f, 0.0f, kBarBackAlpha * uiAlpha_});
  barBack_->Update(kUvTransform);
  barFill_->SetPosition({menuLeft_, barY_});
  barFill_->SetSize({barWidth_ * shown, barHeight_});
  barFill_->SetColor(white);
  barFill_->Update(kUvTransform);

  // 選択肢：選んでいる方を濃く、左に印を付ける
  const float choiceLeft = menuLeft_ + markerWidth_ + markerGap_;
  const bool isRetry = focused_ == Choice::Retry;
  retry_->SetPosition({choiceLeft, retryY_});
  retry_->SetColor(
      {1.0f, 1.0f, 1.0f, (isRetry ? 1.0f : unfocusedAlpha_) * uiAlpha_});
  retry_->Update(kUvTransform);
  stay_->SetPosition({choiceLeft, stayY_});
  stay_->SetColor(
      {1.0f, 1.0f, 1.0f, (isRetry ? unfocusedAlpha_ : 1.0f) * uiAlpha_});
  stay_->Update(kUvTransform);
  marker_->SetPosition({menuLeft_, isRetry ? retryY_ : stayY_});
  marker_->SetSize({markerWidth_, kMarkerHeight});
  marker_->SetColor(white);
  marker_->Update(kUvTransform);
}

void StageSelectReturn::Draw() {
  if (!isActive_ || uiAlpha_ <= 0.0f) {
    return;
  }
  label_->Draw();
  for (int i = 0; i < digitCount_; ++i) {
    digits_[i]->Draw();
  }
  barBack_->Draw();
  barFill_->Draw();
  marker_->Draw();
  retry_->Draw();
  stay_->Draw();
}

void StageSelectReturn::DrawImGui() {
#ifdef USE_IMGUI
  if (ImGui::Button("Save Return Config")) {
    SaveConfig();
  }
  ImGui::SameLine();
  if (ImGui::Button("Load Return Config")) {
    LoadConfig();
  }
  ImGui::Text("Time %.2f (phase %d)", timer_, static_cast<int>(phase_));
  // 止めて、つまみで時刻を動かし、その時刻の構図を見る（選ぶ前だけ）
  if (phase_ == Phase::Wake || phase_ == Phase::Menu) {
    ImGui::Checkbox("Pause", &isPaused_);
    if (isPaused_) {
      ImGui::SliderFloat("Scrub Time", &timer_, 0.0f,
                         menuTime_ + menuFadeSec_ + barFillSec_);
    }
  }
  ImGui::SeparatorText("Timing");
  ImGui::DragFloat("Look Up Time", &lookUpTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Look Up Sec", &lookUpSec_, 0.01f, 0.01f, 5.0f);
  ImGui::DragFloat("Menu Time", &menuTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Menu Fade Sec", &menuFadeSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Bar Fill Sec", &barFillSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Leave Sec", &leaveSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Stay Camera Sec", &stayCameraSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Glow Return Time", &glowReturnTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Glow Return Sec", &glowReturnSec_, 0.01f, 0.01f, 5.0f);
  ImGui::SeparatorText("Dragon");
  ImGui::DragFloat("Lie Depth", &lieDepth_, 0.01f, 0.0f, 3.0f);
  ImGui::SliderFloat("Chest Raise", &chestRaise_, 0.0f, 1.0f);
  ImGui::SliderFloat("Wing Motion", &wingMotion_, 0.0f, 1.0f);
  ImGui::DragFloat("Stir Sec", &stirSec_, 0.01f, 0.01f, 5.0f);
  ImGui::DragFloat4("Lie Wing", &lieWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::DragFloat4("Stir Wing", &stirWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::DragFloat4("Stand Wing", &standWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::DragFloat("Return Pattern", &returnPattern_, 0.005f, 0.0f, 2.0f);
  ImGui::SeparatorText("Camera Keys (right, up, forward)");
  ImGui::SliderFloat("Descent Start Speed", &descentStartSpeed_, 0.0f, 3.0f);
  ImGui::Checkbox("Mirror Camera", &isCameraMirrored_);
  for (size_t i = 0; i < cameraKeys_.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text("Key %d", static_cast<int>(i));
    ImGui::DragFloat("Time", &cameraKeys_[i].time, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat3("Camera", &cameraKeys_[i].cameraOffset.x, 0.05f);
    ImGui::DragFloat3("Look", &cameraKeys_[i].lookOffset.x, 0.05f);
    ImGui::PopID();
  }
  ImGui::SeparatorText("UI");
  ImGui::DragFloat("Menu Left", &menuLeft_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Label Y", &labelY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Digits Left", &digitsLeft_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Digit Advance", &digitAdvance_, 0.5f, 0.0f, 100.0f);
  ImGui::DragFloat("Bar Y", &barY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Bar Width", &barWidth_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Bar Height", &barHeight_, 0.5f, 1.0f, 50.0f);
  ImGui::DragFloat("Retry Y", &retryY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Stay Y", &stayY_, 1.0f, 0.0f, 720.0f);
  ImGui::SliderFloat("Unfocused Alpha", &unfocusedAlpha_, 0.0f, 1.0f);
#endif
}
