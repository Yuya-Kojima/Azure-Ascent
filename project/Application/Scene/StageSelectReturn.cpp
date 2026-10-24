#include "StageSelectReturn.h"
#include "Input/Input.h"
#include "Input/InputKeyState.h"
#include "Sprite/Sprite.h"
#include <algorithm>
#include <cmath>
#include <string>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
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

void StageSelectReturn::Initialize(SpriteRenderer *spriteRenderer) {
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
    UpdateCamera();
    if (timer_ >= menuTime_) {
      phase_ = Phase::Menu;
      phaseTimer_ = timer_ - menuTime_;
    }
    break;
  case Phase::Menu:
    UpdateWake();
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
  // 倒れている → 頭を上げる → 前脚をついて起き上がる → 首を立てて空を見上げる
  //===========================
  const float headUp = EaseInOutCubic(
      Progress(timer_, headUpTime_, riseTime_ - headUpTime_));
  const float rise =
      EaseInOutCubic(Progress(timer_, riseTime_, lookUpTime_ - riseTime_));
  const float lookUp =
      EaseInOutCubic(Progress(timer_, lookUpTime_, menuTime_ - lookUpTime_));

  // 倒れている間は沈めて、前へ傾け、横へ傾ける。起き上がるにつれて戻す
  const float lying = 1.0f - rise;
  dragonPos_ = basePos_ - kWorldUp * (lieDepth_ * lying);
  dragonRot_ = {baseRot_.x + liePitch_ * lying, baseRot_.y,
                baseRot_.z + lieRoll_ * lying};

  gesture_ = {};
  gesture_.collapse = Lerp(1.0f, headUpCollapse_, headUp) * lying;
  gesture_.lookUp = lookUp;

  if (timer_ < lookUpTime_) {
    wingState_ = LerpWing(lieWing_, riseWing_, rise);
  } else {
    wingState_ = LerpWing(riseWing_, standWing_, lookUp);
  }
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
  cameraPos_ = ToWorld(SampleCameraKeys(&CameraKey::cameraOffset));
  const Vector3 lookPos = ToWorld(SampleCameraKeys(&CameraKey::lookOffset));
  cameraRot_ = LookRotation(cameraPos_, lookPos);
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
    leaveLookUp_ = gesture_.lookUp;
    leaveCameraPos_ = cameraPos_;
    leaveCameraRot_ = cameraRot_;
  }
}

void StageSelectReturn::UpdateLeave() {
  //===========================
  // UI と見上げる姿勢を消す。拠点に残るときは、カメラをいつもの位置へ戻す
  //===========================
  const float fade = Progress(phaseTimer_, 0.0f, leaveSec_);
  uiAlpha_ = 1.0f - fade;
  gesture_ = {};
  gesture_.lookUp = leaveLookUp_ * (1.0f - EaseInOutQuad(fade));
  dragonPos_ = basePos_;
  dragonRot_ = baseRot_;
  wingState_ = standWing_;

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
  ImGui::DragFloat("Head Up Time", &headUpTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Rise Time", &riseTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Look Up Time", &lookUpTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Menu Time", &menuTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Menu Fade Sec", &menuFadeSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Bar Fill Sec", &barFillSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Leave Sec", &leaveSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Stay Camera Sec", &stayCameraSec_, 0.01f, 0.01f, 3.0f);
  ImGui::SeparatorText("Dragon");
  ImGui::DragFloat("Lie Depth", &lieDepth_, 0.01f, 0.0f, 3.0f);
  ImGui::SliderAngle("Lie Pitch", &liePitch_, -60.0f, 60.0f);
  ImGui::SliderAngle("Lie Roll", &lieRoll_, -60.0f, 60.0f);
  ImGui::SliderFloat("Head Up Collapse", &headUpCollapse_, 0.0f, 1.0f);
  ImGui::DragFloat4("Lie Wing", &lieWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::DragFloat4("Rise Wing", &riseWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::DragFloat4("Stand Wing", &standWing_.strength, 0.01f, 0.0f, 4.0f);
  ImGui::SeparatorText("Camera Keys (right, up, forward)");
  ImGui::SliderFloat("Descent Start Speed", &descentStartSpeed_, 0.0f, 3.0f);
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
