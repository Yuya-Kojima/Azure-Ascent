#include "Effect/BigEnemyDeath.h"
#include "../../externals/nlohmann/json.hpp"
#include "Effect/EffectManager.h"
#include "Effect/FractureDebris.h"
#include "Math/MathUtil.h"
#include "Render/Particle/IParticleEmitter.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

namespace {
const char *kConfigPath = "resources/config/BigEnemyDeathConfig.json";

// 層の、JSON のキーと ImGui の見出し（BigEnemyDeath::Layer の順）
const char *const kLayerNames[] = {"flash", "fire", "spark", "smoke", "flame"};
const char *const kLayerLabels[] = {(const char *)u8"閃光 (Flash)", (const char *)u8"炎 (Fire)",
                                    (const char *)u8"火花 (Spark)", (const char *)u8"黒煙 (Smoke)",
                                    (const char *)u8"燃え続ける炎 (Flame)"};

constexpr float kHalfTurn = 3.14159265f; // 生まれたときの回転のばらつき（-π〜π ラジアン）

// 層の見た目（重さ・大きさ・寿命・色・回転）を出す頼みに入れる
void ApplyLayer(const BigEnemyDeath::LayerConfig &layer, float scaleRate, ParticleEmitDesc &desc) {
  desc.weightMin = layer.weightMin;
  desc.weightMax = layer.weightMax;
  desc.baseScale = Vector3{layer.scaleMin, layer.scaleMin, layer.scaleMin} * scaleRate;
  const float scaleRange = (layer.scaleMax - layer.scaleMin) * scaleRate;
  desc.scaleRandom = {scaleRange, scaleRange, scaleRange};
  desc.lifeMin = layer.lifeMin;
  desc.lifeMax = layer.lifeMax;
  desc.scaleEndRate = layer.scaleEndRate;
  desc.fadeStart = layer.fadeStart;
  desc.color = {layer.colorStart.x, layer.colorStart.y, layer.colorStart.z, 1.0f};
  desc.colorEnd = layer.colorEnd;
  desc.rotateRandom = {kHalfTurn, kHalfTurn, kHalfTurn};
  desc.rotateVelocityRandom = {layer.spin, layer.spin, layer.spin};
}

// Vector3 を JSON の配列にする・JSON の配列から読む
nlohmann::json ToJson(const Vector3 &v) { return {v.x, v.y, v.z}; }
void FromJson(const nlohmann::json &j, Vector3 &v) {
  if (j.is_array() && j.size() == 3) {
    v = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
  }
}
} // namespace

void BigEnemyDeath::Start(const Vector3 &localHitPoint, const Vector3 &localHitNormal) {
  localHitPoint_ = localHitPoint;
  localHitNormal_ = localHitNormal;
  phase_ = Phase::Crack;
  timer_ = 0.0f;
  frontRadius_ = 0.0f;
  sparkCarry_ = 0.0f;
  smokeCarry_ = 0.0f;
  hasCrackStarted_ = false;
  hasBlastSmoked_ = false;
  events_.push_back(Event::Stop);
}

void BigEnemyDeath::Update(const Matrix4x4 &ownerFrame, const std::vector<Box> &boxes,
                           const Vector3 &ownerVelocity, const Vector3 &cameraPosition) {
  if (phase_ == Phase::None || phase_ == Phase::Finished) {
    return;
  }
  const Config &config = EffectManager::GetInstance()->GetDeathConfig();
  timer_ += kFrameTime_;
  // 炎の模様を持ち主と一緒に動かす
  EffectManager::GetInstance()->SetDeathFireOrigin(
      {ownerFrame.m[3][0], ownerFrame.m[3][1], ownerFrame.m[3][2]});

  switch (phase_) {
  case Phase::Crack: {
    // 撃ち抜いた点は持ち主と一緒に動くので、毎フレーム持ち主の中からワールドへ直す
    const Vector3 hitPoint = TransformPoint(localHitPoint_, ownerFrame);
    const Vector3 hitNormal = Normalize(TransformNormal(localHitNormal_, ownerFrame));
    UpdateCrack(config, boxes, hitPoint, hitNormal, ownerVelocity);
    // 決めた秒数が過ぎたら大爆発
    if (timer_ >= config.crackSeconds) {
      Blast(config, boxes, ownerVelocity, cameraPosition);
    }
    break;
  }
  case Phase::Aftermath:
    // 黒煙は火の玉より少し遅れて、同じ所から出す
    if (!hasBlastSmoked_ && timer_ >= config.blastSmokeDelay) {
      hasBlastSmoked_ = true;
      EmitBurst(Layer::Smoke, blastCenter_, blastSpread_, config.blastSmokeCount, ownerVelocity,
                config.blastSmokeSpeed, config.blastSmokeScale);
    }
    if (timer_ >= config.aftermathSeconds) {
      phase_ = Phase::Finished;
    }
    break;
  default:
    break;
  }
}

void BigEnemyDeath::UpdateDamage(const std::vector<Box> &boxes, const Vector3 &ownerVelocity) {
  if (boxes.empty()) {
    return;
  }
  const Config &config = EffectManager::GetInstance()->GetDeathConfig();
  // 耐え始めの最初のフレームは、間隔を待たずに 1 発出す
  const bool isFirst = damageElapsed_ <= 0.0f;
  damageElapsed_ += kFrameTime_;
  damageTimer_ += kFrameTime_;

  // 耐えた時間の割合で、間隔と大きさを始めの値から激しくなりきった値へ寄せる
  const float ramp =
      std::clamp(damageElapsed_ / (std::max)(config.damageRampSeconds, 1e-3f), 0.0f, 1.0f);
  const float interval =
      config.damageIntervalStart + (config.damageIntervalEnd - config.damageIntervalStart) * ramp;
  if (!isFirst && damageTimer_ < interval) {
    return;
  }
  damageTimer_ = 0.0f;
  const float scaleRate =
      config.damageScaleStart + (config.damageScaleEnd - config.damageScaleStart) * ramp;

  // 箱を 1 つ選び、面を広さに比べた割合で選ぶ（x の面・y の面・z の面）
  const Box &box = boxes[std::uniform_int_distribution<size_t>(0, boxes.size() - 1)(random_)];
  const Vector3 &h = box.halfSize;
  const float areaX = h.y * h.z;
  const float areaY = h.x * h.z;
  const float areaZ = h.x * h.y;
  const float pick = std::uniform_real_distribution<float>(0.0f, areaX + areaY + areaZ)(random_);
  const float sign = std::bernoulli_distribution(0.5)(random_) ? 1.0f : -1.0f;

  // 選んだ面の上の点（箱の中の座標）と、面の外向き
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  Vector3 local = {unit(random_) * h.x, unit(random_) * h.y, unit(random_) * h.z};
  Vector3 localNormal = {0.0f, 0.0f, 0.0f};
  if (pick < areaX) {
    local.x = sign * h.x;
    localNormal.x = sign;
  } else if (pick < areaX + areaY) {
    local.y = sign * h.y;
    localNormal.y = sign;
  } else {
    local.z = sign * h.z;
    localNormal.z = sign;
  }
  const Vector3 point = TransformPoint(local, box.world);
  const Vector3 normal = Normalize(TransformNormal(localNormal, box.world));

  EmitSmallExplosion(config, point, normal, ownerVelocity, scaleRate);
}

void BigEnemyDeath::EmitSmallExplosion(const Config &config, const Vector3 &point,
                                       const Vector3 &normal, const Vector3 &ownerVelocity,
                                       float scaleRate) {
  const Vector3 spread = {config.damageFireSpread, config.damageFireSpread, config.damageFireSpread};
  // 閃光と火の玉は持ち主の速さを受け継いで表面に付いたままふくらむ
  EmitBurst(Layer::Flash, point, {0.0f, 0.0f, 0.0f}, 1, ownerVelocity, 0.0f,
            config.damageFlashScale * scaleRate);
  EmitBurst(Layer::Fire, point, spread * scaleRate, config.damageFireCount,
            ownerVelocity + normal * config.damageFireSpeed, config.damageFireSpeed * 0.5f,
            config.damageFireScale * scaleRate);
  EmitBurst(Layer::Spark, point, {0.0f, 0.0f, 0.0f}, config.damageSparkCount,
            ownerVelocity + normal * config.damageSparkSpeed, config.damageSparkSpeed * 0.5f);
  // 黒煙は持ち主の速さを少しだけ受け継ぎ、持ち主が進むと後ろに置いていかれる
  EmitBurst(Layer::Smoke, point, {0.0f, 0.0f, 0.0f}, config.damageSmokeCount,
            ownerVelocity * config.damageSmokeInherit + normal * config.damageSmokeSpeed,
            config.damageSmokeSpeed, config.damageSmokeScale * scaleRate);
}

void BigEnemyDeath::UpdateCrack(const Config &config, const std::vector<Box> &boxes,
                                const Vector3 &hitPoint, const Vector3 &hitNormal,
                                const Vector3 &ownerVelocity) {
  // 割れ始め: 最初のフレームに、割れ目の出発点で一番大きな小さな爆発を起こす
  if (!hasCrackStarted_) {
    hasCrackStarted_ = true;
    EmitSmallExplosion(config, hitPoint, hitNormal, ownerVelocity, config.damageFinalScale);
  }

  // 前線: 割れ目が走る長さで、撃ち抜いた点から体の一番遠い角まで走る
  float farthest = 0.0f;
  for (const Box &box : boxes) {
    const Vector3 localHit = TransformPoint(hitPoint, Inverse(box.world));
    const Vector3 farCorner = {localHit.x > 0.0f ? -box.halfSize.x : box.halfSize.x,
                               localHit.y > 0.0f ? -box.halfSize.y : box.halfSize.y,
                               localHit.z > 0.0f ? -box.halfSize.z : box.halfSize.z};
    farthest = (std::max)(farthest, Length(localHit - farCorner));
  }
  const float progress = std::clamp(timer_ / (std::max)(config.crackSeconds, 1e-3f), 0.0f, 1.0f);
  frontRadius_ = farthest * progress;

  // 塊を箱の今の位置に置き直し、前線が過ぎた塊の割れ目を開く
  FractureDebris::Opening opening;
  opening.frontCenter = hitPoint;
  opening.frontRadius = frontRadius_;
  opening.openWidth = config.crackOpenWidth;
  opening.gap = config.crackGap;
  for (const Box &box : boxes) {
    if (box.chunks) {
      box.chunks->Hold(box.world, opening);
    }
  }

  // 開いた割れ目から、決めた数（1 秒あたり）の火花と煙を噴く
  sparkCarry_ += config.crackSparkRate * kFrameTime_;
  smokeCarry_ += config.crackSmokeRate * kFrameTime_;
  for (; sparkCarry_ >= 1.0f; sparkCarry_ -= 1.0f) {
    EmitCrackJet(Layer::Spark, boxes, ownerVelocity, config.crackSparkSpeed,
                 config.crackSparkSpread, 1.0f);
  }
  for (; smokeCarry_ >= 1.0f; smokeCarry_ -= 1.0f) {
    EmitCrackJet(Layer::Smoke, boxes, ownerVelocity, config.crackSmokeSpeed,
                 config.crackSmokeSpread, config.crackSmokeScale);
  }
}

bool BigEnemyDeath::EmitCrackJet(Layer layer, const std::vector<Box> &boxes,
                                 const Vector3 &ownerVelocity, float speed, float spread,
                                 float scaleRate) {
  if (boxes.empty()) {
    return false;
  }
  // 箱を 1 つ選んで割れ目の点を探す。その箱に開いた割れ目がなければ、次の箱を順に見る
  const size_t first = std::uniform_int_distribution<size_t>(0, boxes.size() - 1)(random_);
  for (size_t i = 0; i < boxes.size(); ++i) {
    const Box &box = boxes[(first + i) % boxes.size()];
    Vector3 point;
    Vector3 normal;
    if (!box.chunks || !box.chunks->PickOpenCrackPoint(random_, point, normal)) {
      continue;
    }
    EmitBurst(layer, point, {0.0f, 0.0f, 0.0f}, 1, ownerVelocity + normal * speed, spread,
              scaleRate);
    return true;
  }
  return false;
}

void BigEnemyDeath::Blast(const Config &config, const std::vector<Box> &boxes,
                          const Vector3 &ownerVelocity, const Vector3 &cameraPosition) {
  phase_ = Phase::Aftermath;
  timer_ = 0.0f;
  events_.push_back(Event::Blast);
  if (boxes.empty()) {
    return;
  }

  // 体の中心は、箱の中心を体積で重みづけした平均。散らす範囲は一番大きい箱に合わせる
  Vector3 center = {0.0f, 0.0f, 0.0f};
  float totalVolume = 0.0f;
  Vector3 spread = {0.0f, 0.0f, 0.0f};
  for (const Box &box : boxes) {
    const float volume = box.halfSize.x * box.halfSize.y * box.halfSize.z;
    center = center + Vector3{box.world.m[3][0], box.world.m[3][1], box.world.m[3][2]} * volume;
    totalVolume += volume;
    if (volume >= spread.x * spread.y * spread.z) {
      spread = box.halfSize * config.blastSpreadRate;
    }
  }
  center = center * (1.0f / (std::max)(totalVolume, 1e-6f));

  // 閃光: 大きな芯と、芯より長く残るまわりの光を、体の中心からカメラの方へずらした所に出す
  const Vector3 toCamera = cameraPosition - center;
  const float cameraDistance = Length(toCamera);
  Vector3 flashPosition = center;
  if (cameraDistance > 1e-3f) {
    const float shift = (std::min)(config.blastFlashTowardCamera, cameraDistance * 0.5f);
    flashPosition = center + toCamera * (shift / cameraDistance);
  }
  EmitBurst(Layer::Flash, flashPosition, {0.0f, 0.0f, 0.0f}, config.blastFlashCount, ownerVelocity,
            0.0f, config.blastFlashScale, config.blastFlashLifeRate, config.blastFlashIntensity);
  EmitBurst(Layer::Flash, flashPosition, {0.0f, 0.0f, 0.0f}, config.blastGlowCount, ownerVelocity,
            0.0f, config.blastGlowScale, config.blastGlowLifeRate, config.blastGlowIntensity);
  // 空気のゆがみの輪を、大爆発の中心から広げる
  if (config.blastShockwave) {
    EffectManager::GetInstance()->PlayShockwave(center);
  }
  // 炎: 開いた割れ目の線から面の外へ噴く（塊と塊の間から出て、塊を隠さない）
  for (int i = 0; i < config.blastFireCount; ++i) {
    if (!EmitCrackJet(Layer::Fire, boxes, ownerVelocity, config.blastFireSpeed,
                      config.blastFireSpread, config.blastFireScale)) {
      EmitBurst(Layer::Fire, center, spread, 1, ownerVelocity, config.blastFireSpeed,
                config.blastFireScale);
    }
  }
  EmitBurst(Layer::Spark, center, spread, config.blastSparkCount, ownerVelocity,
            config.blastSparkSpeed);
  // 火の粉: 火花の層に、遅く長く残る粒を出す
  EmitBurst(Layer::Spark, center, spread, config.blastEmberCount, ownerVelocity,
            config.blastEmberSpeed, 1.0f, config.blastEmberLifeRate);
  blastCenter_ = center;
  blastSpread_ = spread;
}

void BigEnemyDeath::EmitBurst(Layer layer, const Vector3 &position, const Vector3 &spread,
                              int count, const Vector3 &baseVelocity, float speed,
                              float scaleRate, float lifeRate, float intensity) {
  IParticleEmitter *emitter = EffectManager::GetInstance()->GetDeathEmitter(layer);
  if (!emitter || count <= 0) {
    return;
  }
  ParticleEmitDesc desc{};
  ApplyLayer(EffectManager::GetInstance()->GetDeathConfig().layers[static_cast<int>(layer)],
             scaleRate, desc);
  desc.lifeMin *= lifeRate;
  desc.lifeMax *= lifeRate;
  desc.color.x *= intensity;
  desc.color.y *= intensity;
  desc.color.z *= intensity;
  if (desc.colorEnd) {
    *desc.colorEnd = *desc.colorEnd * intensity;
  }
  desc.position = position;
  desc.spawnRandom = spread;
  desc.count = static_cast<uint32_t>(count);
  desc.baseVelocity = baseVelocity;
  desc.velocityRandom = {speed, speed, speed};
  // 閃光は縦・横・奥を同じ大きさにして、1 粒ずつ出す
  // 大きさは、層の大きさの範囲を出す数で等間隔に分けた値にする
  if (layer == Layer::Flash) {
    const float smallest = desc.baseScale.x;
    const float range = desc.scaleRandom.x;
    desc.scaleRandom = {0.0f, 0.0f, 0.0f};
    desc.count = 1;
    for (int i = 0; i < count; ++i) {
      const float size =
          smallest + range * (static_cast<float>(i) + 0.5f) / static_cast<float>(count);
      desc.baseScale = {size, size, size};
      emitter->Emit(desc);
    }
    return;
  }
  emitter->Emit(desc);
}

void BigEnemyDeath::EmitFlame(const Vector3 &position, const Vector3 &velocity, int count) {
  EmitBurst(Layer::Flame, position, {0.0f, 0.0f, 0.0f}, count, velocity,
            EffectManager::GetInstance()->GetDeathConfig().flameSpeed);
}

void BigEnemyDeath::EmitTrailSmoke(const Vector3 &position, const Vector3 &velocity, int count) {
  const Config &config = EffectManager::GetInstance()->GetDeathConfig();
  EmitBurst(Layer::Smoke, position, {0.0f, 0.0f, 0.0f}, count, velocity, config.chunkSmokeSpread,
            config.chunkSmokeScale);
}

std::vector<BigEnemyDeath::Event> BigEnemyDeath::TakeEvents() {
  std::vector<Event> events;
  events.swap(events_);
  return events;
}

void BigEnemyDeath::LoadConfig(Config &config) {
  std::ifstream file(kConfigPath);
  if (!file.is_open()) {
    return;
  }
  nlohmann::json root;
  try {
    file >> root;
  } catch (...) {
    return;
  }
  auto readFloat = [](const nlohmann::json &item, const char *key, float &value) {
    if (item.contains(key)) {
      value = item[key].get<float>();
    }
  };
  auto readVector = [](const nlohmann::json &item, const char *key, Vector3 &value) {
    if (item.contains(key)) {
      FromJson(item[key], value);
    }
  };
  readFloat(root, "crackSeconds", config.crackSeconds);
  readFloat(root, "aftermathSeconds", config.aftermathSeconds);
  auto readInt = [&root](const char *key, int &value) {
    if (root.contains(key)) {
      value = root[key].get<int>();
    }
  };
  readFloat(root, "crackOpenWidth", config.crackOpenWidth);
  readFloat(root, "crackGap", config.crackGap);
  readFloat(root, "sparkStreakSeconds", config.sparkStreakSeconds);
  readFloat(root, "sparkStreakWidthRate", config.sparkStreakWidthRate);
  readFloat(root, "crackSparkRate", config.crackSparkRate);
  readFloat(root, "crackSparkSpeed", config.crackSparkSpeed);
  readFloat(root, "crackSparkSpread", config.crackSparkSpread);
  readFloat(root, "crackSmokeRate", config.crackSmokeRate);
  readFloat(root, "crackSmokeSpeed", config.crackSmokeSpeed);
  readFloat(root, "crackSmokeSpread", config.crackSmokeSpread);
  readFloat(root, "crackSmokeScale", config.crackSmokeScale);
  readFloat(root, "flameSpeed", config.flameSpeed);
  readInt("blastFlashCount", config.blastFlashCount);
  readFloat(root, "blastFlashScale", config.blastFlashScale);
  readFloat(root, "blastFlashLifeRate", config.blastFlashLifeRate);
  readFloat(root, "blastFlashIntensity", config.blastFlashIntensity);
  readInt("blastGlowCount", config.blastGlowCount);
  readFloat(root, "blastGlowScale", config.blastGlowScale);
  readFloat(root, "blastGlowLifeRate", config.blastGlowLifeRate);
  readFloat(root, "blastGlowIntensity", config.blastGlowIntensity);
  readFloat(root, "blastFlashTowardCamera", config.blastFlashTowardCamera);
  if (root.contains("blastShockwave")) {
    config.blastShockwave = root["blastShockwave"].get<bool>();
  }
  readInt("blastFireCount", config.blastFireCount);
  readFloat(root, "blastFireSpeed", config.blastFireSpeed);
  readFloat(root, "blastFireSpread", config.blastFireSpread);
  readFloat(root, "blastFireScale", config.blastFireScale);
  readInt("blastSparkCount", config.blastSparkCount);
  readFloat(root, "blastSparkSpeed", config.blastSparkSpeed);
  readInt("blastSmokeCount", config.blastSmokeCount);
  readFloat(root, "blastSmokeSpeed", config.blastSmokeSpeed);
  readFloat(root, "blastSmokeScale", config.blastSmokeScale);
  readFloat(root, "blastSmokeDelay", config.blastSmokeDelay);
  readInt("blastEmberCount", config.blastEmberCount);
  readFloat(root, "blastEmberSpeed", config.blastEmberSpeed);
  readFloat(root, "blastEmberLifeRate", config.blastEmberLifeRate);
  readFloat(root, "blastSpreadRate", config.blastSpreadRate);
  readInt("chunkSeedCount", config.chunkSeedCount);
  readInt("chunkLargeSeedCount", config.chunkLargeSeedCount);
  readFloat(root, "chunkSpeed", config.chunkSpeed);
  readFloat(root, "chunkGravity", config.chunkGravity);
  readFloat(root, "chunkSpin", config.chunkSpin);
  readFloat(root, "chunkLifetime", config.chunkLifetime);
  readFloat(root, "chunkDrag", config.chunkDrag);
  readFloat(root, "chunkColorRate", config.chunkColorRate);
  readVector(root, "chunkCapColor", config.chunkCapColor);
  readFloat(root, "chunkCapIntensity", config.chunkCapIntensity);
  readFloat(root, "chunkCoolSeconds", config.chunkCoolSeconds);
  readFloat(root, "chunkCooledRate", config.chunkCooledRate);
  readFloat(root, "chunkFlameRate", config.chunkFlameRate);
  readFloat(root, "chunkSmokeRate", config.chunkSmokeRate);
  readFloat(root, "chunkTrailInherit", config.chunkTrailInherit);
  readFloat(root, "chunkSmokeScale", config.chunkSmokeScale);
  readFloat(root, "chunkSmokeSpread", config.chunkSmokeSpread);
  readFloat(root, "damageIntervalStart", config.damageIntervalStart);
  readFloat(root, "damageIntervalEnd", config.damageIntervalEnd);
  readFloat(root, "damageScaleStart", config.damageScaleStart);
  readFloat(root, "damageScaleEnd", config.damageScaleEnd);
  readFloat(root, "damageRampSeconds", config.damageRampSeconds);
  readFloat(root, "damageFinalScale", config.damageFinalScale);
  readFloat(root, "damageFlashScale", config.damageFlashScale);
  readInt("damageFireCount", config.damageFireCount);
  readFloat(root, "damageFireSpeed", config.damageFireSpeed);
  readFloat(root, "damageFireSpread", config.damageFireSpread);
  readFloat(root, "damageFireScale", config.damageFireScale);
  readInt("damageSparkCount", config.damageSparkCount);
  readFloat(root, "damageSparkSpeed", config.damageSparkSpeed);
  readInt("damageSmokeCount", config.damageSmokeCount);
  readFloat(root, "damageSmokeSpeed", config.damageSmokeSpeed);
  readFloat(root, "damageSmokeScale", config.damageSmokeScale);
  readFloat(root, "damageSmokeInherit", config.damageSmokeInherit);
  for (int i = 0; i < kLayerCount; ++i) {
    if (!root.contains("layers") || !root["layers"].contains(kLayerNames[i])) {
      continue;
    }
    const nlohmann::json &item = root["layers"][kLayerNames[i]];
    LayerConfig &layer = config.layers[i];
    readFloat(item, "weightMin", layer.weightMin);
    readFloat(item, "weightMax", layer.weightMax);
    readFloat(item, "scaleMin", layer.scaleMin);
    readFloat(item, "scaleMax", layer.scaleMax);
    readFloat(item, "lifeMin", layer.lifeMin);
    readFloat(item, "lifeMax", layer.lifeMax);
    readFloat(item, "scaleEndRate", layer.scaleEndRate);
    readFloat(item, "fadeStart", layer.fadeStart);
    readFloat(item, "spin", layer.spin);
    readVector(item, "colorStart", layer.colorStart);
    readVector(item, "colorEnd", layer.colorEnd);
    readVector(item, "gravity", layer.gravity);
    readVector(item, "wind", layer.wind);
    readFloat(item, "drag", layer.drag);
  }
}

void BigEnemyDeath::SaveConfig(const Config &config) {
  nlohmann::json root;
  root["crackSeconds"] = config.crackSeconds;
  root["aftermathSeconds"] = config.aftermathSeconds;
  root["crackOpenWidth"] = config.crackOpenWidth;
  root["crackGap"] = config.crackGap;
  root["sparkStreakSeconds"] = config.sparkStreakSeconds;
  root["sparkStreakWidthRate"] = config.sparkStreakWidthRate;
  root["crackSparkRate"] = config.crackSparkRate;
  root["crackSparkSpeed"] = config.crackSparkSpeed;
  root["crackSparkSpread"] = config.crackSparkSpread;
  root["crackSmokeRate"] = config.crackSmokeRate;
  root["crackSmokeSpeed"] = config.crackSmokeSpeed;
  root["crackSmokeSpread"] = config.crackSmokeSpread;
  root["crackSmokeScale"] = config.crackSmokeScale;
  root["flameSpeed"] = config.flameSpeed;
  root["blastFlashCount"] = config.blastFlashCount;
  root["blastFlashScale"] = config.blastFlashScale;
  root["blastFlashLifeRate"] = config.blastFlashLifeRate;
  root["blastFlashIntensity"] = config.blastFlashIntensity;
  root["blastGlowCount"] = config.blastGlowCount;
  root["blastGlowScale"] = config.blastGlowScale;
  root["blastGlowLifeRate"] = config.blastGlowLifeRate;
  root["blastGlowIntensity"] = config.blastGlowIntensity;
  root["blastFlashTowardCamera"] = config.blastFlashTowardCamera;
  root["blastShockwave"] = config.blastShockwave;
  root["blastFireCount"] = config.blastFireCount;
  root["blastFireSpeed"] = config.blastFireSpeed;
  root["blastFireSpread"] = config.blastFireSpread;
  root["blastFireScale"] = config.blastFireScale;
  root["blastSparkCount"] = config.blastSparkCount;
  root["blastSparkSpeed"] = config.blastSparkSpeed;
  root["blastSmokeCount"] = config.blastSmokeCount;
  root["blastSmokeSpeed"] = config.blastSmokeSpeed;
  root["blastSmokeScale"] = config.blastSmokeScale;
  root["blastSmokeDelay"] = config.blastSmokeDelay;
  root["blastEmberCount"] = config.blastEmberCount;
  root["blastEmberSpeed"] = config.blastEmberSpeed;
  root["blastEmberLifeRate"] = config.blastEmberLifeRate;
  root["blastSpreadRate"] = config.blastSpreadRate;
  root["chunkSeedCount"] = config.chunkSeedCount;
  root["chunkLargeSeedCount"] = config.chunkLargeSeedCount;
  root["chunkSpeed"] = config.chunkSpeed;
  root["chunkGravity"] = config.chunkGravity;
  root["chunkSpin"] = config.chunkSpin;
  root["chunkLifetime"] = config.chunkLifetime;
  root["chunkDrag"] = config.chunkDrag;
  root["chunkColorRate"] = config.chunkColorRate;
  root["chunkCapColor"] = ToJson(config.chunkCapColor);
  root["chunkCapIntensity"] = config.chunkCapIntensity;
  root["chunkCoolSeconds"] = config.chunkCoolSeconds;
  root["chunkCooledRate"] = config.chunkCooledRate;
  root["chunkFlameRate"] = config.chunkFlameRate;
  root["chunkSmokeRate"] = config.chunkSmokeRate;
  root["chunkTrailInherit"] = config.chunkTrailInherit;
  root["chunkSmokeScale"] = config.chunkSmokeScale;
  root["chunkSmokeSpread"] = config.chunkSmokeSpread;
  root["damageIntervalStart"] = config.damageIntervalStart;
  root["damageIntervalEnd"] = config.damageIntervalEnd;
  root["damageScaleStart"] = config.damageScaleStart;
  root["damageScaleEnd"] = config.damageScaleEnd;
  root["damageRampSeconds"] = config.damageRampSeconds;
  root["damageFinalScale"] = config.damageFinalScale;
  root["damageFlashScale"] = config.damageFlashScale;
  root["damageFireCount"] = config.damageFireCount;
  root["damageFireSpeed"] = config.damageFireSpeed;
  root["damageFireSpread"] = config.damageFireSpread;
  root["damageFireScale"] = config.damageFireScale;
  root["damageSparkCount"] = config.damageSparkCount;
  root["damageSparkSpeed"] = config.damageSparkSpeed;
  root["damageSmokeCount"] = config.damageSmokeCount;
  root["damageSmokeSpeed"] = config.damageSmokeSpeed;
  root["damageSmokeScale"] = config.damageSmokeScale;
  root["damageSmokeInherit"] = config.damageSmokeInherit;
  for (int i = 0; i < kLayerCount; ++i) {
    const LayerConfig &layer = config.layers[i];
    nlohmann::json item;
    item["weightMin"] = layer.weightMin;
    item["weightMax"] = layer.weightMax;
    item["scaleMin"] = layer.scaleMin;
    item["scaleMax"] = layer.scaleMax;
    item["lifeMin"] = layer.lifeMin;
    item["lifeMax"] = layer.lifeMax;
    item["scaleEndRate"] = layer.scaleEndRate;
    item["fadeStart"] = layer.fadeStart;
    item["spin"] = layer.spin;
    item["colorStart"] = ToJson(layer.colorStart);
    item["colorEnd"] = ToJson(layer.colorEnd);
    item["gravity"] = ToJson(layer.gravity);
    item["wind"] = ToJson(layer.wind);
    item["drag"] = layer.drag;
    root["layers"][kLayerNames[i]] = item;
  }

  if (!std::filesystem::exists("resources/config")) {
    std::filesystem::create_directories("resources/config");
  }
  std::ofstream file(kConfigPath);
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
  }
}

bool BigEnemyDeath::DrawConfigUI(Config &config) {
  bool changed = false;
#ifdef USE_IMGUI
  ImGui::SeparatorText((const char *)u8"時間");
  changed |= ImGui::DragFloat((const char *)u8"割れ目が走る長さ (秒)", &config.crackSeconds, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragFloat((const char *)u8"余韻の長さ (秒)", &config.aftermathSeconds, 0.01f, 0.0f, 10.0f);

  ImGui::SeparatorText((const char *)u8"耐える間");
  changed |= ImGui::DragFloat((const char *)u8"倒した直後の間隔 (秒)", &config.damageIntervalStart, 0.005f, 0.02f, 3.0f);
  changed |= ImGui::DragFloat((const char *)u8"激しくなりきった間隔 (秒)", &config.damageIntervalEnd, 0.005f, 0.02f, 3.0f);
  changed |= ImGui::DragFloat((const char *)u8"倒した直後の大きさの倍率", &config.damageScaleStart, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragFloat((const char *)u8"激しくなりきった大きさの倍率", &config.damageScaleEnd, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragFloat((const char *)u8"激しくなりきるまで (秒)", &config.damageRampSeconds, 0.05f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"割れ始めの爆発の大きさの倍率", &config.damageFinalScale, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"閃光の大きさの倍率##damage", &config.damageFlashScale, 0.01f, 0.0f, 3.0f);
  changed |= ImGui::DragInt((const char *)u8"火の玉の粒の数##damage", &config.damageFireCount, 1.0f, 0, 100);
  changed |= ImGui::DragFloat((const char *)u8"火の玉がふくらむ速さ (m/秒)##damage", &config.damageFireSpeed, 0.1f, 0.0f, 50.0f);
  changed |= ImGui::DragFloat((const char *)u8"火の玉を散らす範囲 (m)##damage", &config.damageFireSpread, 0.05f, 0.0f, 20.0f);
  changed |= ImGui::DragFloat((const char *)u8"火の玉の大きさの倍率##damage", &config.damageFireScale, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragInt((const char *)u8"火花の数##damage", &config.damageSparkCount, 1.0f, 0, 500);
  changed |= ImGui::DragFloat((const char *)u8"火花の速さ (m/秒)##damage", &config.damageSparkSpeed, 0.1f, 0.0f, 200.0f);
  changed |= ImGui::DragInt((const char *)u8"黒煙の数##damage", &config.damageSmokeCount, 1.0f, 0, 100);
  changed |= ImGui::DragFloat((const char *)u8"黒煙の速さ (m/秒)##damage", &config.damageSmokeSpeed, 0.1f, 0.0f, 50.0f);
  changed |= ImGui::DragFloat((const char *)u8"黒煙の大きさの倍率##damage", &config.damageSmokeScale, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragFloat((const char *)u8"黒煙が受け継ぐ速さの割合##damage", &config.damageSmokeInherit, 0.01f, 0.0f, 1.0f);

  ImGui::SeparatorText((const char *)u8"火花の見た目");
  changed |= ImGui::DragFloat((const char *)u8"線の長さ (速さ × 秒)", &config.sparkStreakSeconds, 0.005f, 0.0f, 1.0f);
  changed |= ImGui::DragFloat((const char *)u8"線の太さの倍率", &config.sparkStreakWidthRate, 0.01f, 0.01f, 2.0f);

  ImGui::SeparatorText((const char *)u8"割れ目");
  changed |= ImGui::DragFloat((const char *)u8"開ききるまでの距離 (m)", &config.crackOpenWidth, 0.1f, 0.1f, 50.0f);
  changed |= ImGui::DragFloat((const char *)u8"開く幅 (m)", &config.crackGap, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く火花の数 (1 秒あたり)", &config.crackSparkRate, 1.0f, 0.0f, 5000.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く火花の速さ (m/秒)", &config.crackSparkSpeed, 0.1f, 0.0f, 200.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く火花のばらつき (m/秒)", &config.crackSparkSpread, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く煙の数 (1 秒あたり)", &config.crackSmokeRate, 0.5f, 0.0f, 1000.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く煙の速さ (m/秒)", &config.crackSmokeSpeed, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く煙のばらつき (m/秒)", &config.crackSmokeSpread, 0.1f, 0.0f, 50.0f);
  changed |= ImGui::DragFloat((const char *)u8"噴く煙の大きさの倍率", &config.crackSmokeScale, 0.01f, 0.0f, 5.0f);

  ImGui::SeparatorText((const char *)u8"燃え続ける炎");
  changed |= ImGui::DragFloat((const char *)u8"燃え続ける炎の速さ (m/秒)", &config.flameSpeed, 0.05f, 0.0f, 50.0f);

  ImGui::SeparatorText((const char *)u8"大爆発");
  changed |= ImGui::DragInt((const char *)u8"閃光の芯の数", &config.blastFlashCount, 1.0f, 0, 50);
  changed |= ImGui::DragFloat((const char *)u8"閃光の芯の大きさの倍率", &config.blastFlashScale, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"閃光の芯の寿命の倍率", &config.blastFlashLifeRate, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"閃光の芯の明るさの倍率", &config.blastFlashIntensity, 0.05f, 0.0f, 50.0f);
  changed |= ImGui::DragInt((const char *)u8"まわりの光の数", &config.blastGlowCount, 1.0f, 0, 50);
  changed |= ImGui::DragFloat((const char *)u8"まわりの光の大きさの倍率", &config.blastGlowScale, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"まわりの光の寿命の倍率", &config.blastGlowLifeRate, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"まわりの光の明るさの倍率", &config.blastGlowIntensity, 0.05f, 0.0f, 50.0f);
  changed |= ImGui::DragFloat((const char *)u8"閃光をカメラの方へずらす距離 (m)", &config.blastFlashTowardCamera, 0.5f, 0.0f, 200.0f);
  changed |= ImGui::Checkbox((const char *)u8"空気のゆがみの輪", &config.blastShockwave);
  changed |= ImGui::DragInt((const char *)u8"割れ目から噴く炎の数", &config.blastFireCount, 1.0f, 0, 200);
  changed |= ImGui::DragFloat((const char *)u8"炎が噴く速さ (m/秒)", &config.blastFireSpeed, 0.1f, 0.0f, 200.0f);
  changed |= ImGui::DragFloat((const char *)u8"炎の速さのばらつき (m/秒)", &config.blastFireSpread, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"炎の大きさの倍率", &config.blastFireScale, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragInt((const char *)u8"火花の数", &config.blastSparkCount, 1.0f, 0, 50000);
  changed |= ImGui::DragFloat((const char *)u8"火花の速さ (m/秒)", &config.blastSparkSpeed, 0.1f, 0.0f, 300.0f);
  changed |= ImGui::DragInt((const char *)u8"黒煙の数", &config.blastSmokeCount, 1.0f, 0, 5000);
  changed |= ImGui::DragFloat((const char *)u8"黒煙の速さ (m/秒)", &config.blastSmokeSpeed, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"黒煙の大きさの倍率", &config.blastSmokeScale, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"黒煙を出すまで (秒)", &config.blastSmokeDelay, 0.01f, 0.0f, 2.0f);
  changed |= ImGui::DragInt((const char *)u8"火の粉の数", &config.blastEmberCount, 1.0f, 0, 5000);
  changed |= ImGui::DragFloat((const char *)u8"火の粉の速さ (m/秒)", &config.blastEmberSpeed, 0.1f, 0.0f, 200.0f);
  changed |= ImGui::DragFloat((const char *)u8"火の粉の寿命の倍率", &config.blastEmberLifeRate, 0.01f, 0.1f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"散らす範囲の倍率", &config.blastSpreadRate, 0.01f, 0.0f, 3.0f);

  ImGui::SeparatorText((const char *)u8"塊");
  changed |= ImGui::SliderInt((const char *)u8"箱 1 個を割る数", &config.chunkSeedCount, 1, 16);
  changed |= ImGui::SliderInt((const char *)u8"一番大きい箱を割る数", &config.chunkLargeSeedCount, 1, 32);
  ImGui::TextDisabled((const char *)u8"割る数は次に出た敵から反映されます");
  changed |= ImGui::DragFloat((const char *)u8"外向きの速さ (m/秒)##chunk", &config.chunkSpeed, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"落ちる加速度 (m/秒²)##chunk", &config.chunkGravity, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"回る速さの最大 (ラジアン/秒)##chunk", &config.chunkSpin, 0.01f, 0.0f, 20.0f);
  changed |= ImGui::DragFloat((const char *)u8"消えるまで (秒)##chunk", &config.chunkLifetime, 0.01f, 0.1f, 20.0f);
  changed |= ImGui::DragFloat((const char *)u8"空気の抵抗 (1/秒)##chunk", &config.chunkDrag, 0.01f, 0.0f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"色 (体の色に対する倍率)##chunk", &config.chunkColorRate, 0.01f, 0.0f, 2.0f);
  changed |= ImGui::ColorEdit3((const char *)u8"割れ口の色##chunk", &config.chunkCapColor.x);
  changed |= ImGui::DragFloat((const char *)u8"割れ口の明るさ##chunk", &config.chunkCapIntensity, 0.01f, 0.0f, 20.0f);
  changed |= ImGui::DragFloat((const char *)u8"割れ口が冷めきるまで (秒)##chunk", &config.chunkCoolSeconds, 0.01f, 0.01f, 10.0f);
  changed |= ImGui::DragFloat((const char *)u8"冷めた割れ口の明るさの倍率##chunk", &config.chunkCooledRate, 0.005f, 0.0f, 1.0f);
  changed |= ImGui::DragFloat((const char *)u8"炎の数 (1 秒あたり・塊 1 つ)##chunk", &config.chunkFlameRate, 0.5f, 0.0f, 300.0f);
  changed |= ImGui::DragFloat((const char *)u8"黒煙の数 (1 秒あたり・塊 1 つ)##chunk", &config.chunkSmokeRate, 0.1f, 0.0f, 100.0f);
  changed |= ImGui::DragFloat((const char *)u8"尾が持つ塊の速さの割合##chunk", &config.chunkTrailInherit, 0.01f, 0.0f, 1.0f);
  changed |= ImGui::DragFloat((const char *)u8"尾の黒煙の大きさの倍率##chunk", &config.chunkSmokeScale, 0.01f, 0.0f, 5.0f);
  changed |= ImGui::DragFloat((const char *)u8"尾の黒煙のばらつき (m/秒)##chunk", &config.chunkSmokeSpread, 0.1f, 0.0f, 50.0f);

  ImGui::SeparatorText((const char *)u8"粒の層");
  for (int i = 0; i < kLayerCount; ++i) {
    LayerConfig &layer = config.layers[i];
    if (!ImGui::TreeNode(kLayerLabels[i])) {
      continue;
    }
    changed |= ImGui::DragFloatRange2((const char *)u8"重さ", &layer.weightMin, &layer.weightMax, 0.01f, 0.01f, 20.0f);
    changed |= ImGui::DragFloatRange2((const char *)u8"大きさ (m)", &layer.scaleMin, &layer.scaleMax, 0.05f, 0.01f, 100.0f);
    changed |= ImGui::DragFloatRange2((const char *)u8"寿命 (秒)", &layer.lifeMin, &layer.lifeMax, 0.01f, 0.02f, 10.0f);
    changed |= ImGui::DragFloat((const char *)u8"終わりの大きさの倍率", &layer.scaleEndRate, 0.01f, 0.0f, 5.0f);
    changed |= ImGui::DragFloat((const char *)u8"薄くなり始め (寿命の割合)", &layer.fadeStart, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat((const char *)u8"回る速さ (ラジアン/秒)", &layer.spin, 0.1f, 0.0f, 60.0f);
    changed |= ImGui::ColorEdit3((const char *)u8"始まりの色", &layer.colorStart.x);
    changed |= ImGui::ColorEdit3((const char *)u8"終わりの色", &layer.colorEnd.x);
    changed |= ImGui::DragFloat3((const char *)u8"重力 (m/秒²)", &layer.gravity.x, 0.1f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat3((const char *)u8"風 (m/秒)", &layer.wind.x, 0.1f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat((const char *)u8"空気の抵抗 (1/秒)", &layer.drag, 0.01f, 0.0f, 20.0f);
    ImGui::TreePop();
  }
#endif
  return changed;
}
