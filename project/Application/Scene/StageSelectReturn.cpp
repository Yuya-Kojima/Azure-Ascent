#include "StageSelectReturn.h"
#include "../../externals/nlohmann/json.hpp"
#include "Framework/MenuInput.h"
#include "Input/Input.h"
#include "SkipHold.h"
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
// 文字の画像のまわりの縁（影）の余白（px）。make_intro_text.py の縁のぼかし半径 × 3
const float kTitlePad = 15.0f;  // 空域名（ぼかし 5）
const float kChoicePad = 15.0f; // 選択肢（ぼかし 5）
const float kLabelPad = 12.0f;  // 線の下の名前（ぼかし 4）
// 空路の線と印の大きさ（px）
const float kTrackHeight = 2.0f;     // 暗い線の太さ
const float kCoreHeight = 2.0f;      // 水色の線の芯の太さ
const float kGlowHeight = 18.0f;     // 水色の線のまわりの光の高さ
const float kDiamondSize = 34.0f;    // ひし形の画像の大きさ（光の余白を含む）
const float kDotSize = 16.0f;        // 点の画像の大きさ
const float kMarkPopScale = 0.7f;    // 線が着いた所の印が、着いた瞬間に大きくなる量（1 に足す倍率）
const float kBossPopWhiten = 0.6f;   // ボスの印が光る瞬間に、白へ寄せる割合
const float kUnderlineHeight = 2.0f; // 選んでいる方の下線の太さ
// UI の後ろの板
const float kScreenWidth = 1280.0f;  // 画面の幅（px）。板の右端
const float kPlateEdgeWidth = 1.0f;  // 板の縁の線の太さ（px）
// 色（敵の赤・竜の水色・空域名と同じ金）
const Vector3 kGold = {0.91f, 0.78f, 0.51f};
const Vector3 kCyan = {0.5f, 0.88f, 0.94f};
const Vector3 kCyanCore = {0.86f, 0.98f, 1.0f};
const Vector3 kBossRed = {1.0f, 0.2f, 0.27f};
const Vector3 kTrackGray = {0.75f, 0.77f, 0.8f};
const Vector3 kPlateColor = {0.03f, 0.04f, 0.07f}; // UI の後ろの板（暗い空に寄せた黒）
// 不透明度
const float kPlateEdgeAlpha = 0.6f;  // 板の縁の線
const float kTrackAlpha = 0.35f;     // 出撃 〜 ボスの暗い線
const float kGlowAlpha = 0.75f;      // 水色の線のまわりの光
const float kStartMarkAlpha = 0.7f;  // 出撃の点

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
  visit("time.routeFadeSec", routeFadeSec_);
  visit("time.routeFillSec", routeFillSec_);
  visit("time.choiceFadeSec", choiceFadeSec_);
  visit("time.markPopSec", markPopSec_);
  visit("time.focusMoveSec", focusMoveSec_);
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

  visit("ui.menuLeft", menuLeft_);
  visit("ui.plateLeft", plateLeft_);
  visit("ui.plateTop", plateTop_);
  visit("ui.plateHeight", plateHeight_);
  visit("ui.plateAlpha", plateAlpha_);
  visit("ui.titleY", titleY_);
  visit("ui.routeY", routeY_);
  visit("ui.routeWidth", routeWidth_);
  visit("ui.routeLabelY", routeLabelY_);
  visit("ui.retryY", retryY_);
  visit("ui.stayY", stayY_);
  visit("ui.underlineOffset", underlineOffset_);
  visit("ui.unfocusedBrightness", unfocusedBrightness_);
  visit("ui.unfocusedAlpha", unfocusedAlpha_);

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

  // 画像を読み、基準点（anchor）を決めたスプライトを作る。大きさは画像のまま
  auto create = [spriteRenderer](const std::string &path,
                                 const Vector2 &anchor) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, path);
    sprite->SetAnchorPoint(anchor);
    return sprite;
  };
  const Vector2 kLeftCenter = {0.0f, 0.5f};
  const Vector2 kCenter = {0.5f, 0.5f};
  const std::string dir = "resources/UI/GameOver/";
  plate_ = create("resources/white1x1.png", {0.0f, 0.0f});
  for (auto &edge : plateEdges_) {
    edge = create("resources/white1x1.png", {0.0f, 0.0f});
  }
  title_ = create(dir + "route_title.png", kLeftCenter);
  routeTrack_ = create("resources/white1x1.png", kLeftCenter);
  routeGlow_ = create(dir + "route_glow.png", kLeftCenter);
  routeCore_ = create("resources/white1x1.png", kLeftCenter);
  startMark_ = create(dir + "route_dot.png", kCenter);
  reachMark_ = create(dir + "route_diamond.png", kCenter);
  bossMark_ = create(dir + "route_diamond.png", kCenter);
  startLabel_ = create(dir + "route_start.png", kCenter);
  bossLabel_ = create(dir + "route_boss.png", kCenter);
  retry_ = create(dir + "retry.png", kLeftCenter);
  stay_ = create(dir + "stay.png", kLeftCenter);
  underline_ = create("resources/white1x1.png", kLeftCenter);
  skipHold_ = std::make_unique<SkipHold>();
  skipHold_->Initialize(spriteRenderer);
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
  leaveAlpha_ = 1.0f;
  focusPos_ = 0.0f;
  skipHold_->Start();

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

  // 起き上がりの間だけ、決定の長押しで UI が出る所まで飛ばせる
  const bool isSkipped = skipHold_->Update(
      input, phase_ == Phase::Wake && timer_ >= skipLockSec_);

  switch (phase_) {
  case Phase::Wake:
    if (isSkipped) {
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
  // 下線を、選んでいる方へ動かす
  const float focusTarget = (focused_ == Choice::Retry) ? 0.0f : 1.0f;
  const float focusStep =
      focusMoveSec_ > 0.0f ? kFrameSec / focusMoveSec_ : 1.0f;
  focusPos_ += std::clamp(focusTarget - focusPos_, -focusStep, focusStep);

  if (!input) {
    return;
  }
  // 選択肢が現れきる前に決定を押すと、現れきった所まで飛ばす（その押下では選ばない）
  const float readySec = GetChoiceReadySec();
  if (phaseTimer_ < readySec) {
    if (MenuInput::IsDecideTriggered(input)) {
      timer_ = menuTime_ + readySec;
      phaseTimer_ = readySec;
    }
    return;
  }
  if (MenuInput::IsUpTriggered(input) || MenuInput::IsDownTriggered(input)) {
    focused_ = (focused_ == Choice::Retry) ? Choice::Stay : Choice::Retry;
  }

  if (MenuInput::IsDecideTriggered(input)) {
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
  leaveAlpha_ = 1.0f - fade;
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
  //===========================
  // UI が出てからの時刻で、空域名と暗い線 → 水色の線 → 選択肢の順に出す
  //===========================
  const float t = timer_ - menuTime_;
  // 空域名と選択肢の左端は、出撃の点の真下に置く「出撃」の文字の左端にそろえる
  const float textLeft =
      menuLeft_ - (startLabel_->GetSize().x - kLabelPad * 2.0f) * 0.5f;
  const bool isShown = phase_ != Phase::Wake;
  const float routeAlpha =
      isShown ? EaseOutCubic(Progress(t, 0.0f, routeFadeSec_)) * leaveAlpha_
              : 0.0f;
  const float fillStart = routeFadeSec_;
  const float fillEnd = fillStart + routeFillSec_;
  const float fill = EaseInOutCubic(Progress(t, fillStart, routeFillSec_));
  const float choiceAlpha =
      isShown ? EaseOutCubic(Progress(t, fillEnd, choiceFadeSec_)) * leaveAlpha_
              : 0.0f;
  // 線が着いた瞬間に印が大きく光り、落ち着く（1 → 0）
  const float pop = isShown && t >= fillEnd
                        ? 1.0f - EaseOutCubic(Progress(t, fillEnd, markPopSec_))
                        : 0.0f;

  // 線の位置（出撃・ボス・落ちた所）
  const float startX = menuLeft_;
  const float bossX = menuLeft_ + routeWidth_;
  const float reachX = Lerp(startX, bossX, progress_);
  const bool isBossReached = progress_ >= 1.0f;
  const float fillWidth = (reachX - startX) * fill;

  auto place = [](Sprite &sprite, const Vector2 &pos, const Vector2 &size,
                  const Vector3 &color, float alpha) {
    sprite.SetPosition(pos);
    sprite.SetSize(size);
    sprite.SetColor({color.x, color.y, color.z, alpha});
    sprite.Update(kUvTransform);
  };
  // 画像の大きさのまま置く
  auto placeImage = [](Sprite &sprite, const Vector2 &pos, const Vector3 &color,
                       float alpha) {
    sprite.SetPosition(pos);
    sprite.SetColor({color.x, color.y, color.z, alpha});
    sprite.Update(kUvTransform);
  };
  const Vector3 kWhite = {1.0f, 1.0f, 1.0f};

  // 後ろの板と縁の線は、空域名と同時に現れる
  const float plateWidth = kScreenWidth - plateLeft_;
  const float plateBottom = plateTop_ + plateHeight_ - kPlateEdgeWidth;
  const float edgeAlpha = kPlateEdgeAlpha * routeAlpha;
  place(*plate_, {plateLeft_, plateTop_}, {plateWidth, plateHeight_},
        kPlateColor, plateAlpha_ * routeAlpha);
  place(*plateEdges_[0], {plateLeft_, plateTop_},
        {plateWidth, kPlateEdgeWidth}, kGold, edgeAlpha);
  place(*plateEdges_[1], {plateLeft_, plateTop_},
        {kPlateEdgeWidth, plateHeight_}, kGold, edgeAlpha);
  place(*plateEdges_[2], {plateLeft_, plateBottom},
        {plateWidth, kPlateEdgeWidth}, kGold, edgeAlpha);

  // 空域名と暗い線
  placeImage(*title_, {textLeft - kTitlePad, titleY_}, kWhite, routeAlpha);
  place(*routeTrack_, {startX, routeY_}, {bossX - startX, kTrackHeight},
        kTrackGray, kTrackAlpha * routeAlpha);

  // 水色の線（光と芯）
  place(*routeGlow_, {startX, routeY_}, {fillWidth, kGlowHeight}, kCyan,
        kGlowAlpha * routeAlpha);
  place(*routeCore_, {startX, routeY_}, {fillWidth, kCoreHeight}, kCyanCore,
        routeAlpha);

  // 印と、その下の名前
  place(*startMark_, {startX, routeY_}, {kDotSize, kDotSize}, kTrackGray,
        kStartMarkAlpha * routeAlpha);
  // ボスの印：ボス戦まで来ていたら、線が着いた瞬間に大きく白く光る
  const float bossPop = isBossReached ? pop : 0.0f;
  const float bossSize = kDiamondSize * (1.0f + kMarkPopScale * bossPop);
  place(*bossMark_, {bossX, routeY_}, {bossSize, bossSize},
        Lerp(kBossRed, kWhite, bossPop * kBossPopWhiten), routeAlpha);
  // 落ちた所の印：道中で落ちたときだけ、線が着いてから現れる
  const float reachAlpha =
      (!isBossReached && isShown && t >= fillEnd) ? leaveAlpha_ : 0.0f;
  const float reachSize = kDiamondSize * (1.0f + kMarkPopScale * pop);
  place(*reachMark_, {reachX, routeY_}, {reachSize, reachSize},
        Lerp(kCyan, kCyanCore, pop), reachAlpha);
  placeImage(*startLabel_, {startX, routeLabelY_}, kWhite, routeAlpha);
  placeImage(*bossLabel_, {bossX, routeLabelY_}, kWhite, routeAlpha);

  //===========================
  // 選択肢：選んでいない方を透かし（unfocusedBrightness_ で暗くもできる）、選んでいる方に下線を引く
  //===========================
  const float choiceLeft = textLeft - kChoicePad;
  const Vector3 dim = {unfocusedBrightness_, unfocusedBrightness_,
                       unfocusedBrightness_};
  const float retryFocus = 1.0f - focusPos_;
  const float stayFocus = focusPos_;
  placeImage(*retry_, {choiceLeft, retryY_}, Lerp(dim, kWhite, retryFocus),
             Lerp(unfocusedAlpha_, 1.0f, retryFocus) * choiceAlpha);
  placeImage(*stay_, {choiceLeft, stayY_}, Lerp(dim, kWhite, stayFocus),
             Lerp(unfocusedAlpha_, 1.0f, stayFocus) * choiceAlpha);
  // 下線の長さは、選んでいる方の文字の幅（縁の余白を除く）
  const float retryWidth = retry_->GetSize().x - kChoicePad * 2.0f;
  const float stayWidth = stay_->GetSize().x - kChoicePad * 2.0f;
  const float easedFocus = EaseInOutCubic(focusPos_);
  place(*underline_,
        {textLeft, Lerp(retryY_, stayY_, easedFocus) + underlineOffset_},
        {Lerp(retryWidth, stayWidth, easedFocus), kUnderlineHeight}, kGold,
        choiceAlpha);
}

void StageSelectReturn::Draw() {
  // 長押しの案内は UI の板より奥に出す（UI が現れたら板が上に重なる）
  if (isActive_) {
    skipHold_->Draw();
  }
  if (!isActive_ || phase_ == Phase::Wake || leaveAlpha_ <= 0.0f) {
    return;
  }
  plate_->Draw();
  for (auto &edge : plateEdges_) {
    edge->Draw();
  }
  routeTrack_->Draw();
  routeGlow_->Draw();
  routeCore_->Draw();
  startMark_->Draw();
  bossMark_->Draw();
  reachMark_->Draw();
  startLabel_->Draw();
  bossLabel_->Draw();
  title_->Draw();
  underline_->Draw();
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
                         menuTime_ + GetChoiceReadySec() + markPopSec_);
    }
  }
  ImGui::SeparatorText("Timing");
  ImGui::DragFloat("Look Up Time", &lookUpTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Look Up Sec", &lookUpSec_, 0.01f, 0.01f, 5.0f);
  ImGui::DragFloat("Menu Time", &menuTime_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Route Fade Sec", &routeFadeSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Route Fill Sec", &routeFillSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Choice Fade Sec", &choiceFadeSec_, 0.01f, 0.0f, 3.0f);
  ImGui::DragFloat("Mark Pop Sec", &markPopSec_, 0.01f, 0.01f, 3.0f);
  ImGui::DragFloat("Focus Move Sec", &focusMoveSec_, 0.01f, 0.0f, 1.0f);
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
  ImGui::DragFloat("Plate Left", &plateLeft_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Plate Top", &plateTop_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Plate Height", &plateHeight_, 1.0f, 0.0f, 720.0f);
  ImGui::SliderFloat("Plate Alpha", &plateAlpha_, 0.0f, 1.0f);
  ImGui::DragFloat("Title Y", &titleY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Route Y", &routeY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Route Width", &routeWidth_, 1.0f, 0.0f, 1280.0f);
  ImGui::DragFloat("Route Label Y", &routeLabelY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Retry Y", &retryY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Stay Y", &stayY_, 1.0f, 0.0f, 720.0f);
  ImGui::DragFloat("Underline Offset", &underlineOffset_, 0.5f, 0.0f, 60.0f);
  ImGui::SliderFloat("Unfocused Brightness", &unfocusedBrightness_, 0.0f, 1.0f);
  ImGui::SliderFloat("Unfocused Alpha", &unfocusedAlpha_, 0.0f, 1.0f);
  ImGui::SliderFloat("Progress", &progress_, 0.0f, 1.0f);
#endif
}
