#include "StageSelectLightning.h"
#include "Camera/ICamera.h"
#include "Render/CloudSea/CloudSea.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/PostProcess.h"
#include "Renderer/TrailRenderer.h"
#include "Texture/TextureManager.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace {
// 真上
const Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
const float kDegToRad = std::numbers::pi_v<float> / 180.0f;

// 稲妻の帯に貼る画像（横の真ん中ほど不透明。縦は同じ）の登録名と大きさ（px）
const char *const kBandTextureName = "StageSelectLightning.band";
const int kBandTextureWidth = 64;
const int kBandTextureHeight = 4;
// 帯の横の端での不透明度が exp(-kBandFalloff) になる
const float kBandFalloff = 4.0f;

// 区間を半分に割る回数（幹: 2^5 + 1 = 33 点、枝: 2^4 + 1 = 17 点）
const int kTrunkLevels = 5;
const int kBranchLevels = 4;
const int kMaxBranches = 4;
// 枝の根元を置く、幹の上の範囲（幹の長さに対する割合）
const float kBranchRootMin = 0.15f;
const float kBranchRootMax = 0.75f;
// 幹の両端の太さ（真ん中に対する割合）と、枝の先の太さ（根元に対する割合）
const float kTrunkEndWidth = 0.3f;
const float kBranchTipWidth = 0.15f;

// 稲妻の線をこれより暗いときは描かない
const float kMinBoltBrightness = 1e-3f;
// 弱い光を数える回数の上限（時刻がとても進んでも止まるように）
const uint32_t kMaxWeakFlashes = 10000;
// 弱い光の間隔の下限（秒）
const float kMinWeakInterval = 0.5f;

/// <summary>
/// 経過時間 elapsed での光の明るさ（0〜1）
/// rise 秒で 1 まで上がり、hold 秒そのまま、fade 秒で 0 まで下がる（下がり方は 2 乗）
/// </summary>
float Envelope(float elapsed, float rise, float hold, float fade) {
  if (elapsed < 0.0f) {
    return 0.0f;
  }
  if (elapsed < rise) {
    return elapsed / rise;
  }
  elapsed -= rise;
  if (elapsed < hold) {
    return 1.0f;
  }
  elapsed -= hold;
  if (fade <= 0.0f || elapsed >= fade) {
    return 0.0f;
  }
  const float remain = 1.0f - elapsed / fade;
  return remain * remain;
}

/// <summary>
/// 種・番号・用途から決まる 0〜1 の値（同じ引数なら毎回同じ）
/// </summary>
float Hash01(uint32_t seed, uint32_t index, uint32_t channel) {
  uint32_t n = seed * 0x9E3779B9u ^ index * 0x85EBCA6Bu ^ channel * 0xC2B2AE35u;
  n ^= n >> 16;
  n *= 0x7FEB352Du;
  n ^= n >> 15;
  n *= 0x846CA68Bu;
  n ^= n >> 16;
  return static_cast<float>(n & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/// <summary>
/// a から b への折れ線を、区間を半分に割って真ん中を横と上下へずらすことを levels 回くり返して作る
/// ずらす量は区間の長さに比例する（細かい段ほど小さくずれる）
/// </summary>
std::vector<Vector3> MakeJaggedLine(const Vector3 &a, const Vector3 &b,
                                    int levels, float jaggedness,
                                    float vertical, std::mt19937 &rng) {
  std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
  std::vector<Vector3> points = {a, b};
  for (int level = 0; level < levels; ++level) {
    std::vector<Vector3> next;
    next.reserve(points.size() * 2 - 1);
    for (size_t i = 0; i + 1 < points.size(); ++i) {
      const Vector3 &p = points[i];
      const Vector3 &q = points[i + 1];
      const Vector3 segment = q - p;
      const float length = Length(segment);
      const Vector3 side = SafeNormalize(Cross(kWorldUp, segment));
      const Vector3 mid = (p + q) * 0.5f +
                          side * (signedUnit(rng) * jaggedness * length) +
                          kWorldUp * (signedUnit(rng) * jaggedness *
                                      vertical * length);
      next.push_back(p);
      next.push_back(mid);
    }
    next.push_back(points.back());
    points = std::move(next);
  }
  return points;
}
} // namespace

void StageSelectLightning::Initialize() {
  // 稲妻の帯に貼る画像：横の真ん中ほど不透明（色は白）
  std::array<uint8_t, kBandTextureWidth * kBandTextureHeight * 4> pixels{};
  for (int y = 0; y < kBandTextureHeight; ++y) {
    for (int x = 0; x < kBandTextureWidth; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / kBandTextureWidth;
      const float fromCenter = u * 2.0f - 1.0f;
      const float alpha = std::exp(-fromCenter * fromCenter * kBandFalloff);
      uint8_t *pixel = &pixels[(y * kBandTextureWidth + x) * 4];
      pixel[0] = 255;
      pixel[1] = 255;
      pixel[2] = 255;
      pixel[3] = static_cast<uint8_t>(std::lround(alpha * 255.0f));
    }
  }
  TextureManager::GetInstance()->LoadTextureFromMemory(
      kBandTextureName, pixels.data(), kBandTextureWidth, kBandTextureHeight);

  GenerateStrikeBolt();
}

std::vector<StageSelectLightning::BoltPath>
StageSelectLightning::GenerateBolt(uint32_t seed, float length,
                                   int branchCount) const {
  //===========================
  // 幹：稲妻の中心を通り、幹の向きへまっすぐ伸ばした線をぎざぎざにする
  // 枝：幹の上の点から、幹の進む向きへ開いた短い線
  // 点は（幹の向き, 上, 横）の位置
  //===========================
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  std::vector<BoltPath> paths;

  const float halfLength = length * 0.5f;
  BoltPath trunk;
  trunk.points = MakeJaggedLine({-halfLength, 0.0f, 0.0f},
                                {halfLength, 0.0f, 0.0f}, kTrunkLevels,
                                boltJaggedness_, boltVertical_, rng);
  const std::vector<Vector3> trunkPoints = trunk.points;
  paths.push_back(std::move(trunk));

  branchCount = std::clamp(branchCount, 0, kMaxBranches);
  for (int i = 0; i < branchCount; ++i) {
    // 根元は幹の上に均等に散らし、少しずらす
    const float rootRatio =
        Lerp(kBranchRootMin, kBranchRootMax,
             (static_cast<float>(i) + unit(rng)) / branchCount);
    const size_t rootIndex = static_cast<size_t>(
        std::lround(rootRatio * static_cast<float>(trunkPoints.size() - 1)));
    const Vector3 root = trunkPoints[rootIndex];

    // 幹の進む向きから、左右どちらかへ開く
    const float sign = unit(rng) < 0.5f ? -1.0f : 1.0f;
    const float angle =
        Lerp(branchAngleMin_, branchAngleMax_, unit(rng)) * kDegToRad * sign;
    const Vector3 direction = {std::cos(angle), 0.0f, std::sin(angle)};
    const float branchLength =
        Lerp(branchLengthMin_, branchLengthMax_, unit(rng)) * length;

    BoltPath branch;
    branch.points = MakeJaggedLine(root, root + direction * branchLength,
                                   kBranchLevels, boltJaggedness_,
                                   boltVertical_, rng);
    branch.width = branchWidth_;
    branch.brightness = branchBrightness_;
    branch.isBranch = true;
    paths.push_back(std::move(branch));
  }
  return paths;
}

void StageSelectLightning::GenerateStrikeBolt() {
  strikeBoltPaths_ =
      GenerateBolt(static_cast<uint32_t>(std::lround(seed_)), boltLength_,
                   static_cast<int>(std::lround(branchCount_)));
  // 弱い光の稲妻も同じ形の値を使うので、次に使うときに作り直す
  weakBoltIndex_ = UINT32_MAX;
}

Vector3 StageSelectLightning::ToWorld(const Vector2 &offset, float y) const {
  Vector3 p = dragonPos_ + right_ * offset.x + forward_ * offset.y;
  p.y = y;
  return p;
}

void StageSelectLightning::Update(float time, const Vector3 &dragonPos,
                                  float dragonYaw, float ceilingBottom) {
  dragonPos_ = dragonPos;
  forward_ = {std::sin(dragonYaw), 0.0f, std::cos(dragonYaw)};
  right_ = Normalize(Cross(kWorldUp, forward_));
  glows_.clear();

  //===========================
  // 大きな雷：雲の中の光と、雲の底を這う稲妻
  //===========================
  const float strike = Envelope(time - strikeTime_, strikeAttackSec_,
                                strikeHoldSec_, strikeFadeSec_);
  const float glowY = ceilingBottom + glowDepth_;
  if (strike > 0.0f) {
    glows_.push_back({ToWorld(strikeOffset_, glowY), strikeGlowRadius_,
                      glowColor_ * (strikeGlowIntensity_ * strike)});
  }
  const float boltY = ceilingBottom + boltLift_;
  const float strikeYaw = boltYaw_ * kDegToRad;
  strikeBolt_.center = ToWorld(strikeOffset_, boltY);
  strikeBolt_.axis =
      forward_ * std::cos(strikeYaw) + right_ * std::sin(strikeYaw);
  strikeBolt_.side = Normalize(Cross(kWorldUp, strikeBolt_.axis));
  strikeBolt_.brightness = std::pow(strike, boltFadePower_);
  strikeBolt_.width = 1.0f;

  //===========================
  // 弱い光と短い稲妻：降りてくる途中に 1 回、大きな雷のあとは間を空けてくり返す
  // 何回目かと種から時刻・向き・距離・稲妻の形が決まるので、時刻を戻しても同じ光になる
  //===========================
  const uint32_t seed = static_cast<uint32_t>(std::lround(seed_));
  const float azimuthRange = weakAzimuthMax_ - weakAzimuthMin_;
  const float intervalMin = (std::max)(weakIntervalMin_, kMinWeakInterval);
  const float intervalMax = (std::max)(weakIntervalMax_, intervalMin);
  float weak = 0.0f;
  weakBolt_.brightness = 0.0f;
  float weakTime = glimmerTime_;
  float prevAzimuth = 0.0f;
  for (uint32_t k = 0; k < kMaxWeakFlashes && weakTime <= time; ++k) {
    // 向き：前の光と近すぎるときは、範囲の半分だけ回して離す
    float azimuth = weakAzimuthMin_ + azimuthRange * Hash01(seed, k, 0);
    if (k > 0 && std::abs(azimuth - prevAzimuth) < weakMinSeparation_) {
      azimuth += azimuthRange * 0.5f;
      if (azimuth > weakAzimuthMax_) {
        azimuth -= azimuthRange;
      }
    }
    prevAzimuth = azimuth;
    const float distance =
        Lerp(weakDistanceMin_, weakDistanceMax_, Hash01(seed, k, 1));

    const float brightness =
        Envelope(time - weakTime, weakRiseSec_, 0.0f, weakFadeSec_);
    if (brightness > 0.0f) {
      const float a = azimuth * kDegToRad;
      const Vector2 offset = {std::sin(a) * distance, std::cos(a) * distance};
      glows_.push_back({ToWorld(offset, glowY), weakGlowRadius_,
                        glowColor_ * (weakGlowIntensity_ * brightness)});
      weak = (std::max)(weak, brightness);

      // 稲妻：ドラゴンから見て横向きを基準に、向きを少しばらつかせる
      if (weakBoltIndex_ != k) {
        weakBoltPaths_ = GenerateBolt(
            seed + k + 1, weakBoltLength_,
            static_cast<int>(std::lround(weakBoltBranchCount_)));
        weakBoltIndex_ = k;
      }
      const float yaw =
          a + (90.0f + (Hash01(seed, k, 3) * 2.0f - 1.0f) * weakBoltYawJitter_) *
                  kDegToRad;
      weakBolt_.center = ToWorld(offset, boltY);
      weakBolt_.axis = forward_ * std::cos(yaw) + right_ * std::sin(yaw);
      weakBolt_.side = Normalize(Cross(kWorldUp, weakBolt_.axis));
      weakBolt_.brightness =
          std::pow(brightness, boltFadePower_) * weakBoltBrightness_;
      weakBolt_.width = weakBoltWidth_;
    }

    // 次の時刻（降りてくる途中の光の次は、大きな雷から数える）
    const float interval = Lerp(intervalMin, intervalMax, Hash01(seed, k, 2));
    weakTime = (k == 0 ? (std::max)(strikeTime_, glimmerTime_) : weakTime) +
               interval;
  }

  flash_ = strike + weak * weakDragonLight_;
}

void StageSelectLightning::ApplyGlows(CloudSea *cloudSea) const {
  cloudSea->ClearGlows();
  const int count =
      (std::min)(static_cast<int>(glows_.size()), CloudSea::kMaxGlows);
  for (int i = 0; i < count; ++i) {
    CloudSeaGlow glow;
    glow.position = glows_[i].position;
    glow.radius = glows_[i].radius;
    glow.color = glows_[i].color;
    cloudSea->SetGlow(i, glow);
  }
}

void StageSelectLightning::ApplyLight(Object3dRenderer *renderer,
                                      float weight) const {
  const float flash = flash_ * weight;
  // 縁の光：いつもの縁の光（色 × 強さ）に、雷の赤を足す
  if (auto *dl = renderer->GetDirectionalLightData()) {
    const Vector3 baseRim = Vector3{dl->backRimColor.x, dl->backRimColor.y,
                                    dl->backRimColor.z} *
                            dl->backRimColor.w;
    const Vector3 rim = baseRim + glowColor_ * (rimIntensity_ * flash);
    dl->backRimColor = {rim.x, rim.y, rim.z, 1.0f};
  }
  // 上から照らす赤い点の光（上を向いた面ほど赤くなる）
  if (auto *pl = renderer->GetPointLightData()) {
    const Vector3 position = dragonPos_ + right_ * dragonLightOffset_.x +
                             kWorldUp * dragonLightOffset_.y +
                             forward_ * dragonLightOffset_.z;
    pl->color = {glowColor_.x, glowColor_.y, glowColor_.z, 1.0f};
    pl->position = position;
    pl->intensity = dragonLightIntensity_ * flash;
    pl->radius = dragonLightRadius_;
    pl->decay = dragonLightDecay_;
  }
}

void StageSelectLightning::ApplyPostProcess(PostProcess *postProcess) const {
  postProcess->SetUseBloom(true);
  postProcess->SetBloomThreshold(bloomThreshold_);
  postProcess->SetBloomIntensity(bloomIntensity_);
  postProcess->SetBloomSigma(bloomSigma_);
}

void StageSelectLightning::AddBoltTrails(const std::vector<BoltPath> &paths,
                                         const BoltPlacement &placement,
                                         const Vector3 &cameraPos) const {
  //===========================
  // 太くて赤いにじみの帯の上に、細くて白に近い芯の帯を重ねる（加算）
  //===========================
  TrailRenderer *trailRenderer = TrailRenderer::GetInstance();
  const Vector3 haloColor =
      glowColor_ * (haloIntensity_ * placement.brightness);
  const Vector3 coreColor =
      coreColor_ * (coreIntensity_ * placement.brightness);
  std::vector<TrailRenderer::TrailNode> halo;
  std::vector<TrailRenderer::TrailNode> core;
  for (const BoltPath &path : paths) {
    const size_t count = path.points.size();
    if (count < 2) {
      continue;
    }
    halo.clear();
    core.clear();
    for (size_t i = 0; i < count; ++i) {
      const float s = static_cast<float>(i) / static_cast<float>(count - 1);
      // 幹は両端、枝は先へ向けて細くする
      const float taper =
          path.isBranch
              ? Lerp(1.0f, kBranchTipWidth, s)
              : Lerp(kTrunkEndWidth, 1.0f,
                     std::sin(s * std::numbers::pi_v<float>));
      const Vector3 &p = path.points[i];
      const Vector3 position = placement.center + placement.axis * p.x +
                               kWorldUp * p.y + placement.side * p.z;
      const float width = placement.width * path.width * taper;
      const Vector3 h = haloColor * path.brightness;
      const Vector3 c = coreColor * path.brightness;
      halo.push_back({position, haloWidth_ * width, {h.x, h.y, h.z, 1.0f}});
      core.push_back({position, coreWidth_ * width, {c.x, c.y, c.z, 1.0f}});
    }
    trailRenderer->AddTrail(halo, cameraPos);
    trailRenderer->AddTrail(core, cameraPos);
  }
}

void StageSelectLightning::Draw(const ICamera *camera) {
  if (!camera) {
    return;
  }
  const bool hasStrike = strikeBolt_.brightness > kMinBoltBrightness;
  const bool hasWeak = weakBolt_.brightness > kMinBoltBrightness;
  if (!hasStrike && !hasWeak) {
    return;
  }
  const Vector3 cameraPos = camera->GetTranslate();
  if (hasStrike) {
    AddBoltTrails(strikeBoltPaths_, strikeBolt_, cameraPos);
  }
  if (hasWeak) {
    AddBoltTrails(weakBoltPaths_, weakBolt_, cameraPos);
  }
  TrailRenderer::GetInstance()->Render(
      camera->GetViewProjectionMatrix(),
      TextureManager::GetInstance()->GetSrvHandleGPU(kBandTextureName));
}

void StageSelectLightning::DrawImGui() {
#ifdef USE_IMGUI
  const ImGuiColorEditFlags kColorFlags =
      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float;
  ImGui::Text("Flash %.2f  Bolt %.2f  Weak Bolt %.2f", flash_,
              strikeBolt_.brightness, weakBolt_.brightness);

  ImGui::SeparatorText("Lightning Timing");
  ImGui::DragFloat("Strike Time", &strikeTime_, 0.01f, 0.0f, 20.0f);
  ImGui::DragFloat("Strike Attack Sec", &strikeAttackSec_, 0.005f, 0.0f, 1.0f);
  ImGui::DragFloat("Strike Hold Sec", &strikeHoldSec_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Strike Fade Sec", &strikeFadeSec_, 0.01f, 0.01f, 5.0f);
  ImGui::DragFloat("Bolt Fade Power", &boltFadePower_, 0.05f, 0.1f, 10.0f);
  ImGui::DragFloat("Glimmer Time", &glimmerTime_, 0.01f, 0.0f, 20.0f);
  ImGui::DragFloat("Weak Interval Min", &weakIntervalMin_, 0.05f, 0.5f, 30.0f);
  ImGui::DragFloat("Weak Interval Max", &weakIntervalMax_, 0.05f, 0.5f, 30.0f);
  ImGui::DragFloat("Weak Rise Sec", &weakRiseSec_, 0.005f, 0.0f, 2.0f);
  ImGui::DragFloat("Weak Fade Sec", &weakFadeSec_, 0.01f, 0.01f, 5.0f);

  ImGui::SeparatorText("Lightning Place (right, forward of dragon)");
  ImGui::DragFloat2("Strike Offset", &strikeOffset_.x, 0.5f);
  ImGui::DragFloat("Glow Depth", &glowDepth_, 0.05f, -10.0f, 20.0f);
  ImGui::DragFloat("Bolt Lift", &boltLift_, 0.05f, -10.0f, 20.0f);
  ImGui::DragFloat("Weak Azimuth Min", &weakAzimuthMin_, 0.5f, -180.0f, 180.0f);
  ImGui::DragFloat("Weak Azimuth Max", &weakAzimuthMax_, 0.5f, -180.0f, 180.0f);
  ImGui::DragFloat("Weak Distance Min", &weakDistanceMin_, 0.5f, 0.0f, 500.0f);
  ImGui::DragFloat("Weak Distance Max", &weakDistanceMax_, 0.5f, 0.0f, 500.0f);
  ImGui::DragFloat("Weak Min Separation", &weakMinSeparation_, 0.5f, 0.0f, 90.0f);

  ImGui::SeparatorText("Cloud Glow");
  ImGui::ColorEdit3("Glow Color", &glowColor_.x, kColorFlags);
  ImGui::DragFloat("Strike Glow Intensity", &strikeGlowIntensity_, 0.05f, 0.0f, 50.0f);
  ImGui::DragFloat("Strike Glow Radius", &strikeGlowRadius_, 0.1f, 0.1f, 200.0f);
  ImGui::DragFloat("Weak Glow Intensity", &weakGlowIntensity_, 0.01f, 0.0f, 50.0f);
  ImGui::DragFloat("Weak Glow Radius", &weakGlowRadius_, 0.1f, 0.1f, 200.0f);

  ImGui::SeparatorText("Bolt Shape");
  bool isShapeChanged = false;
  isShapeChanged |= ImGui::DragFloat("Bolt Yaw", &boltYaw_, 0.5f, -180.0f, 180.0f);
  isShapeChanged |= ImGui::DragFloat("Bolt Length", &boltLength_, 0.1f, 0.1f, 300.0f);
  isShapeChanged |= ImGui::DragFloat("Jaggedness", &boltJaggedness_, 0.005f, 0.0f, 1.0f);
  isShapeChanged |= ImGui::DragFloat("Vertical", &boltVertical_, 0.01f, 0.0f, 2.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Count", &branchCount_, 1.0f, 0.0f,
                                     static_cast<float>(kMaxBranches), "%.0f");
  isShapeChanged |= ImGui::DragFloat("Branch Length Min", &branchLengthMin_, 0.005f, 0.0f, 1.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Length Max", &branchLengthMax_, 0.005f, 0.0f, 1.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Angle Min", &branchAngleMin_, 0.5f, 0.0f, 90.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Angle Max", &branchAngleMax_, 0.5f, 0.0f, 90.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Width", &branchWidth_, 0.01f, 0.0f, 2.0f);
  isShapeChanged |= ImGui::DragFloat("Branch Brightness", &branchBrightness_, 0.01f, 0.0f, 2.0f);
  isShapeChanged |= ImGui::DragFloat("Seed", &seed_, 1.0f, 0.0f, 10000.0f, "%.0f");

  ImGui::SeparatorText("Weak Bolt");
  isShapeChanged |= ImGui::DragFloat("Weak Bolt Length", &weakBoltLength_, 0.1f, 0.1f, 300.0f);
  isShapeChanged |= ImGui::DragFloat("Weak Bolt Branch Count", &weakBoltBranchCount_, 1.0f, 0.0f,
                                     static_cast<float>(kMaxBranches), "%.0f");
  ImGui::DragFloat("Weak Bolt Brightness", &weakBoltBrightness_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Weak Bolt Width", &weakBoltWidth_, 0.01f, 0.0f, 2.0f);
  ImGui::DragFloat("Weak Bolt Yaw Jitter", &weakBoltYawJitter_, 0.5f, 0.0f, 90.0f);
  if (isShapeChanged) {
    GenerateStrikeBolt();
  }

  ImGui::SeparatorText("Bolt Look (before exposure)");
  ImGui::ColorEdit3("Core Color", &coreColor_.x, kColorFlags);
  ImGui::DragFloat("Core Intensity", &coreIntensity_, 0.1f, 0.0f, 100.0f);
  ImGui::DragFloat("Core Width", &coreWidth_, 0.005f, 0.0f, 5.0f);
  ImGui::DragFloat("Halo Intensity", &haloIntensity_, 0.05f, 0.0f, 100.0f);
  ImGui::DragFloat("Halo Width", &haloWidth_, 0.01f, 0.0f, 20.0f);

  ImGui::SeparatorText("Light on Dragon");
  ImGui::DragFloat("Flash Rim Intensity", &rimIntensity_, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Flash Light Intensity", &dragonLightIntensity_, 0.01f, 0.0f, 20.0f);
  ImGui::DragFloat3("Flash Light Offset", &dragonLightOffset_.x, 0.05f);
  ImGui::DragFloat("Flash Light Radius", &dragonLightRadius_, 0.1f, 0.1f, 200.0f);
  ImGui::DragFloat("Flash Light Decay", &dragonLightDecay_, 0.01f, 0.01f, 8.0f);
  ImGui::DragFloat("Weak Dragon Light", &weakDragonLight_, 0.01f, 0.0f, 1.0f);

  ImGui::SeparatorText("Bloom");
  ImGui::DragFloat("Bloom Threshold", &bloomThreshold_, 0.01f, 0.0f, 20.0f);
  ImGui::DragFloat("Bloom Intensity", &bloomIntensity_, 0.01f, 0.0f, 5.0f);
  ImGui::DragFloat("Bloom Sigma", &bloomSigma_, 0.05f, 0.1f, 20.0f);
#endif
}
