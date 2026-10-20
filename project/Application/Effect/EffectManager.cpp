#include "EffectManager.h"
#include "../../externals/nlohmann/json.hpp"
#include "Camera/ICamera.h"
#include "Camera/RailCamera.h"
#include "Render/Renderer/PostProcess.h"
#include "Scene/SceneManager.h"
#ifdef USE_IMGUI
#include <imgui.h>
#endif
#include <filesystem>
#include <fstream>
#include <iomanip>

EffectManager *EffectManager::GetInstance() {
  static EffectManager instance;
  return &instance;
}

void EffectManager::Initialize() {
  LoadShockwaveConfig();
  LoadEffectsConfig();

  activeShockwaves_.clear();
  nextHitEffectIndex_ = 0;

  // 粒の入れ物（GPU バッファ）は最初の1回だけ作り、2回目以降は残っている粒を消す
  if (!hasCreatedParticleGroups_) {
    CreateParticleGroups();
    hasCreatedParticleGroups_ = true;
  } else {
    ClearParticleGroups();
  }

  // ==========================================
  // ボス用エフェクト
  // ==========================================

  // 通常弾の予兆
  bossTelegraphNormalEmitter_ = std::make_unique<ParticleEmitter>(
      bossTelegraphNormalParticleGroup_.get(), Vector3{0.0f, 0.0f, 0.0f},
      Vector3{0.0f, 0.0f, 0.0f}, 1, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
      Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f);                    // 寿命0.5秒
  bossTelegraphNormalEmitter_->SetBaseScale({3.0f, 3.0f, 3.0f}); // 小さめ
  bossTelegraphNormalEmitter_->SetScaleRandom({2.0f, 2.0f, 2.0f});
  bossTelegraphNormalEmitter_->SetColor(
      {5.0f, 1.0f, 0.2f, 1.0f}); // 赤・オレンジ系の強い光
  bossTelegraphNormalEmitter_->SetScaleVelocity(
      {-3.0f, -3.0f, -3.0f}); // 中心に到達する頃には消えるように縮小
  bossTelegraphNormalEmitter_->SetHalfSize(
      {40.0f, 40.0f, 40.0f}); // 広い範囲から発生
  bossTelegraphNormalEmitter_->SetIsConverge(
      true); // 発生位置から中心に向かって飛ぶ

  // 攻撃時エフェクト
  bossBurstEmitter_ = std::make_unique<ParticleEmitter>(
      bossBurstParticleGroup_.get(), Vector3{0.0f, 0.0f, 0.0f},
      Vector3{0.0f, 0.0f, 0.0f}, 1, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
      Vector3{0.0f, 0.0f, 0.0f}, 0.3f, 0.3f); // 発散エフェクトの寿命
  bossBurstEmitter_->SetBaseScale({10.0f, 10.0f, 10.0f}); // 最初は小さめ
  bossBurstEmitter_->SetColor(
      {4.0f, 3.0f, 1.0f, 0.8f}); // 強い白・黄色系でフラッシュ
  bossBurstEmitter_->SetScaleVelocity(
      {400.0f, 400.0f, 400.0f}); // 超高速で膨張する（発散）

  // ==========================================
  // 雑魚敵用エフェクト
  // ==========================================

  // 死亡時エフェクト
  for (int i = 0; i < kMaxHitEffects; ++i) {

    // 1. コア
    deathCoreEmitters_[i] = std::make_unique<ParticleEmitter>(
        hitCoreParticleGroups_[i].get(), Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 1, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 0.5f, 0.5f);
    deathCoreEmitters_[i]->SetBaseScale({20.0f, 20.0f, 20.0f});
    deathCoreEmitters_[i]->SetColor({1.0f, 0.8f, 0.8f, 1.0f});
    deathCoreEmitters_[i]->SetScaleVelocity({-20.0f, -20.0f, -20.0f});

    // 2. フレア
    deathFlareEmitters_[i] = std::make_unique<ParticleEmitter>(
        hitFlareParticleGroups_[i].get(), Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.5f, 0.5f, 0.5f}, 40, 0.0f, Vector3{-30.0f, -30.0f, -30.0f},
        Vector3{30.0f, 30.0f, 30.0f}, 0.4f, 0.6f);
    deathFlareEmitters_[i]->SetBaseScale({0.8f, 0.8f, 0.8f});
    deathFlareEmitters_[i]->SetColor({2.0f, 0.6f, 0.1f, 1.0f});
    deathFlareEmitters_[i]->SetScaleVelocity({-1.0f, -1.0f, -1.0f});

    // 3. リング衝撃波
    deathRingEmitters_[i] = std::make_unique<ParticleEmitter>(
        hitRingParticleGroups_[i].get(), Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 1, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 0.7f, 0.7f);
    deathRingEmitters_[i]->SetBaseScale({0.1f, 0.1f, 0.1f});
    deathRingEmitters_[i]->SetColor({2.0f, 0.2f, 0.1f, 1.0f});
    deathRingEmitters_[i]->SetScaleVelocity({80.0f, 80.0f, 80.0f});

    // 4. ホーミング用グリントエミッター
    hitGlintEmitters_[i] = std::make_unique<ParticleEmitter>(
        hitGlintParticleGroups_[i].get(), Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 1, 0.0f, Vector3{0.0f, 0.0f, 0.0f},
        Vector3{0.0f, 0.0f, 0.0f}, 0.20f, 0.20f);
    hitGlintEmitters_[i]->SetBaseScale({32.0f, 32.0f, 32.0f});
    hitGlintEmitters_[i]->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
    hitGlintEmitters_[i]->SetScaleVelocity({-10.0f, -10.0f, -10.0f});
  }
}

void EffectManager::CreateParticleGroups() {
  // ボス：通常弾の予兆・攻撃時
  bossTelegraphNormalParticleGroup_ =
      std::make_unique<BillboardParticleEmitter>();
  bossTelegraphNormalParticleGroup_->Initialize("resources/circle.png");
  bossTelegraphNormalParticleGroup_->SetIsRingMode(false);

  bossBurstParticleGroup_ = std::make_unique<BillboardParticleEmitter>();
  bossBurstParticleGroup_->Initialize("resources/circle.png");
  bossBurstParticleGroup_->SetIsRingMode(false);

  // 雑魚敵：撃破時のコア・フレア・リング、ホーミング命中時のグリント
  for (int i = 0; i < kMaxHitEffects; ++i) {
    hitCoreParticleGroups_[i] = std::make_unique<BillboardParticleEmitter>();
    hitCoreParticleGroups_[i]->Initialize("resources/circle.png");
    hitFlareParticleGroups_[i] = std::make_unique<BillboardParticleEmitter>();
    hitFlareParticleGroups_[i]->Initialize("resources/circle.png");
    hitRingParticleGroups_[i] = std::make_unique<BillboardParticleEmitter>();
    hitRingParticleGroups_[i]->Initialize("resources/circle.png");
    hitRingParticleGroups_[i]->SetIsRingMode(true);
    hitGlintParticleGroups_[i] = std::make_unique<BillboardParticleEmitter>();
    hitGlintParticleGroups_[i]->Initialize("resources/laser_hit_glint.png");
  }
}

void EffectManager::ClearParticleGroups() {
  bossTelegraphNormalParticleGroup_->ClearParticles();
  bossBurstParticleGroup_->ClearParticles();
  for (int i = 0; i < kMaxHitEffects; ++i) {
    hitCoreParticleGroups_[i]->ClearParticles();
    hitFlareParticleGroups_[i]->ClearParticles();
    hitRingParticleGroups_[i]->ClearParticles();
    hitGlintParticleGroups_[i]->ClearParticles();
  }
}

void EffectManager::Finalize() {
  bossTelegraphNormalEmitter_.reset();
  bossBurstEmitter_.reset();
  bossTelegraphNormalParticleGroup_.reset();
  bossBurstParticleGroup_.reset();
  for (int i = 0; i < kMaxHitEffects; ++i) {
    deathCoreEmitters_[i].reset();
    deathFlareEmitters_[i].reset();
    deathRingEmitters_[i].reset();
    hitGlintEmitters_[i].reset();
    hitCoreParticleGroups_[i].reset();
    hitFlareParticleGroups_[i].reset();
    hitRingParticleGroups_[i].reset();
    hitGlintParticleGroups_[i].reset();
  }
  hasCreatedParticleGroups_ = false;
}

void EffectManager::Update(const ICamera *camera) {
  if (!camera)
    return;
  Matrix4x4 viewProj =
      Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix());

  auto postProcess = SceneManager::GetInstance()->GetCurrentScenePostProcess();
  if (!postProcess)
    return;

  if (!activeShockwaves_.empty()) {
    // タイマー更新
    for (auto it = activeShockwaves_.begin(); it != activeShockwaves_.end();) {
      it->timer -= 1.0f / 60.0f;
      if (it->timer <= 0.0f) {
        it = activeShockwaves_.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (!activeShockwaves_.empty()) {
    postProcess->SetPostEffectType(10); // 10: Shockwave

    std::vector<PostProcess::ShockwaveParams> shockwaveParams;
    for (const auto &sw : activeShockwaves_) {
      Vector3 pos = sw.worldPos;
      float w = pos.x * viewProj.m[0][3] + pos.y * viewProj.m[1][3] +
                pos.z * viewProj.m[2][3] + viewProj.m[3][3];
      if (w <= 0.0f)
        w = 0.0001f;

      Vector3 ndcPos = {(pos.x * viewProj.m[0][0] + pos.y * viewProj.m[1][0] +
                         pos.z * viewProj.m[2][0] + viewProj.m[3][0]) /
                            w,
                        (pos.x * viewProj.m[0][1] + pos.y * viewProj.m[1][1] +
                         pos.z * viewProj.m[2][1] + viewProj.m[3][1]) /
                            w,
                        (pos.x * viewProj.m[0][2] + pos.y * viewProj.m[1][2] +
                         pos.z * viewProj.m[2][2] + viewProj.m[3][2]) /
                            w};

      float uvX = (ndcPos.x + 1.0f) * 0.5f;
      float uvY = (1.0f - ndcPos.y) * 0.5f;
      float t = sw.timer / shockwaveConfig_.duration;

      PostProcess::ShockwaveParams param;
      param.center[0] = uvX;
      param.center[1] = uvY;
      param.radius = (1.0f - t) * shockwaveConfig_.maxRadius;
      param.thickness = shockwaveConfig_.thickness;
      param.weight = t;
      param.distortion = shockwaveConfig_.distortion;

      shockwaveParams.push_back(param);
    }
    postProcess->SetShockwaves(shockwaveParams);
  } else {
    // リセット
    if (postProcess && postProcess->GetPostEffectType() == 10) {
      postProcess->SetPostEffectType(0);
      postProcess->SetShockwaves({});
    }
  }

  // パーティクルの更新
  for (int i = 0; i < kMaxHitEffects; ++i) {
    deathCoreEmitters_[i]->Update();
    deathFlareEmitters_[i]->Update();
    deathRingEmitters_[i]->Update();
    hitCoreParticleGroups_[i]->Update(camera->GetViewMatrix(),
                                      camera->GetProjectionMatrix());
    hitFlareParticleGroups_[i]->Update(camera->GetViewMatrix(),
                                       camera->GetProjectionMatrix());
    hitRingParticleGroups_[i]->Update(camera->GetViewMatrix(),
                                      camera->GetProjectionMatrix());
    hitGlintParticleGroups_[i]->Update(camera->GetViewMatrix(),
                                       camera->GetProjectionMatrix());
  }

  if (bossTelegraphNormalEmitter_) {
    bossTelegraphNormalEmitter_->Update();
    bossTelegraphNormalParticleGroup_->Update(camera->GetViewMatrix(),
                                              camera->GetProjectionMatrix());
  }

  if (bossBurstEmitter_) {
    bossBurstEmitter_->Update();
    bossBurstParticleGroup_->Update(camera->GetViewMatrix(),
                                    camera->GetProjectionMatrix());
  }
}

void EffectManager::Draw() {
  for (int i = 0; i < kMaxHitEffects; ++i) {
    hitCoreParticleGroups_[i]->Draw();
    hitFlareParticleGroups_[i]->Draw();
    hitRingParticleGroups_[i]->Draw();
    hitGlintParticleGroups_[i]->Draw();
  }

  if (bossTelegraphNormalParticleGroup_) {
    bossTelegraphNormalParticleGroup_->Draw();
  }

  if (bossBurstParticleGroup_) {
    bossBurstParticleGroup_->Draw();
  }
}

void EffectManager::PlayBossTelegraphEffect(const Vector3 &center,
                                            const Vector3 &targetPos,
                                            float chargeRatio,
                                            int attackPattern) {
  auto postProcess = SceneManager::GetInstance()->GetCurrentScenePostProcess();

  if (attackPattern == 0) {
    // 通常弾予兆
    if (bossTelegraphNormalEmitter_) {
      bossTelegraphNormalEmitter_->SetCenter(center);

      // 発射直前に発生を止め、粒子がすべて中心に吸い込まれるタメを作る
      if (chargeRatio <= 0.6f) {
        // チャージ進行度に応じて1フレームあたりの発生数を増やす
        int emitCount = 3 + (int)(15.0f * (chargeRatio / 0.6f));
        bossTelegraphNormalEmitter_->SetCount(emitCount);
        bossTelegraphNormalEmitter_->Emit();
      }
    }
  }
}

void EffectManager::PlayBossBurstEffect(const Vector3 &center) {
  if (bossBurstEmitter_) {
    bossBurstEmitter_->SetCenter(center);
    bossBurstEmitter_->Emit();
  }
  // 画面の歪み（ショックウェーブ）も同時に発生させて衝撃を表現
  PlayShockwave(center);
}

void EffectManager::PlayEffect(EffectType type, const Vector3 &worldPos,
                                const Vector4 &color, float scaleMultiplier) {
  size_t typeIndex = static_cast<size_t>(type);
  if (typeIndex >= static_cast<size_t>(EffectType::Count)) {
    return;
  }

  const auto &preset = effectConfigs_[typeIndex];

  // 画面の空間歪みショックウェーブ
  if (preset.enableShockwave) {
    PlayShockwave(worldPos);
  }

  // 1. コア閃光（HomingHitの場合は鋭い十字グリント、その他は円形コア）
  if (preset.core.enable) {
    int i = nextHitEffectIndex_;
    float finalScale = preset.core.scale * scaleMultiplier;
    ParticleEmitter *emitter = (type == EffectType::HomingHit)
                                   ? hitGlintEmitters_[i].get()
                                   : deathCoreEmitters_[i].get();
    emitter->SetBaseScale({finalScale, finalScale, finalScale});
    emitter->SetScaleVelocity({preset.core.scaleVelocity * scaleMultiplier,
                               preset.core.scaleVelocity * scaleMultiplier,
                               preset.core.scaleVelocity * scaleMultiplier});
    emitter->SetLifeRange(preset.core.life, preset.core.life);
    emitter->SetCenter(worldPos);
    emitter->Emit(color);

    nextHitEffectIndex_ = (nextHitEffectIndex_ + 1) % kMaxHitEffects;
  }

  // 2. 散乱火花パーティクル
  if (preset.flare.enable && preset.flare.count > 0) {
    int i = nextHitEffectIndex_;
    float flareScale = preset.flare.scale * scaleMultiplier;
    deathFlareEmitters_[i]->SetBaseScale({flareScale, flareScale, flareScale});
    deathFlareEmitters_[i]->SetScaleVelocity({preset.flare.scaleVelocity,
                                              preset.flare.scaleVelocity,
                                              preset.flare.scaleVelocity});
    deathFlareEmitters_[i]->SetCount(preset.flare.count);
    deathFlareEmitters_[i]->SetLifeRange(preset.flare.lifeMin, preset.flare.lifeMax);
    deathFlareEmitters_[i]->SetBaseVelocity({0.0f, 0.0f, 0.0f});
    deathFlareEmitters_[i]->SetVelocityRandom({preset.flare.speed,
                                               preset.flare.speed,
                                               preset.flare.speed});
    deathFlareEmitters_[i]->SetCenter(worldPos);

    // 火花は少し高輝度に発光
    Vector4 flareColor = {color.x * 1.5f, color.y * 1.5f, color.z * 1.5f, color.w};
    deathFlareEmitters_[i]->Emit(flareColor);

    nextHitEffectIndex_ = (nextHitEffectIndex_ + 1) % kMaxHitEffects;
  }

  // 3. 衝撃波リング
  if (preset.ring.enable && preset.ring.count > 0) {
    for (int j = 0; j < preset.ring.count; ++j) {
      int i = nextHitEffectIndex_;
      float startScale = (preset.ring.startScale + j * 0.15f) * scaleMultiplier;
      float expandSpeed = (preset.ring.expandSpeed - j * 2.0f) * scaleMultiplier;

      deathRingEmitters_[i]->SetBaseScale({startScale, startScale, startScale});
      deathRingEmitters_[i]->SetScaleVelocity({expandSpeed, expandSpeed, expandSpeed});
      deathRingEmitters_[i]->SetLifeRange(preset.ring.life, preset.ring.life);
      deathRingEmitters_[i]->SetCenter(worldPos);

      Vector4 ringColor = {color.x * 1.8f, color.y * 1.8f, color.z * 1.8f, color.w};
      deathRingEmitters_[i]->Emit(ringColor);

      nextHitEffectIndex_ = (nextHitEffectIndex_ + 1) % kMaxHitEffects;
    }
  }
}

void EffectManager::PlayShockwave(const Vector3 &worldPos) {
  if (activeShockwaves_.size() < 5) {
    activeShockwaves_.push_back({shockwaveConfig_.duration, worldPos});
  }
}

void EffectManager::DrawEditorUI(RailCamera *railCamera) {
#ifdef USE_IMGUI
  ImGui::Text("Effect Master Settings");
  ImGui::Separator();

  // Shockwave 設定保存
  if (isShockwaveConfigDirty_ || isEffectsConfigDirty_) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.6f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(0.9f, 0.7f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
  }

  std::string saveButtonText = (isShockwaveConfigDirty_ || isEffectsConfigDirty_)
                                   ? (const char *)u8"[* 未保存] Save All Configs"
                                   : (const char *)u8"Save All Configs";
  if (ImGui::Button(saveButtonText.c_str(),
                    ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
    if (isShockwaveConfigDirty_) {
      SaveShockwaveConfig();
    }
    if (isEffectsConfigDirty_) {
      SaveEffectsConfig();
    }
  }

  if (isShockwaveConfigDirty_ || isEffectsConfigDirty_) {
    ImGui::PopStyleColor(3);
  }

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
  if (ImGui::Button((const char *)u8"▶ Test Play Selected Effect",
                    ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
    if (railCamera) {
      Matrix4x4 viewMatrix = railCamera->GetViewMatrix();
      Matrix4x4 cameraWorld = Inverse(viewMatrix);
      Vector3 cameraPos = {cameraWorld.m[3][0], cameraWorld.m[3][1],
                           cameraWorld.m[3][2]};
      Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                               cameraWorld.m[2][2]};
      Vector3 testPos = {cameraPos.x + cameraForward.x * 25.0f,
                         cameraPos.y + cameraForward.y * 25.0f,
                         cameraPos.z + cameraForward.z * 25.0f};

      EffectType selectedType = static_cast<EffectType>(selectedEffectIndex_);
      Vector4 testColor = {1.0f, 1.0f, 1.0f, 1.0f};
      if (selectedType == EffectType::HitSpark) {
        testColor = {2.0f, 1.4f, 0.4f, 1.0f}; // 通常弾用：オレンジ火花
      } else if (selectedType == EffectType::HomingHit) {
        testColor = {0.6f, 2.5f, 4.0f, 1.0f}; // ホーミング用：高輝度シアンブルーム
      } else if (selectedType == EffectType::EnemyDeath || selectedType == EffectType::EnemyDeathSimple) {
        testColor = {1.0f, 0.6f, 0.2f, 1.0f};
      }
      PlayEffect(selectedType, testPos, testColor);
    }
  }
  ImGui::PopStyleColor();

  ImGui::Spacing();

  // エフェクト選択
  const char *effectNames[] = {
      "HitSpark (通常弾着弾)",
      "HomingHit (ホーミング着弾グリント)",
      "EnemyDeath (大爆発)",
      "EnemyDeathSimple (中爆発)",
      "MuzzleRing (急発進リング)"
  };
  ImGui::Combo((const char *)u8"対象エフェクト", &selectedEffectIndex_, effectNames, IM_ARRAYSIZE(effectNames));

  if (selectedEffectIndex_ >= 0 && selectedEffectIndex_ < static_cast<int>(EffectType::Count)) {
    auto &preset = effectConfigs_[selectedEffectIndex_];

    bool changed = false;
    changed |= ImGui::Checkbox((const char *)u8"空間歪みショックウェーブ発生", &preset.enableShockwave);

    if (ImGui::TreeNode((const char *)u8"コア閃光 (Core Flash)")) {
      changed |= ImGui::Checkbox((const char *)u8"有効 (Core Enable)", &preset.core.enable);
      changed |= ImGui::DragFloat((const char *)u8"初期スケール (Scale)", &preset.core.scale, 0.1f, 0.1f, 50.0f);
      changed |= ImGui::DragFloat((const char *)u8"スケール変化速度 (Scale Vel)", &preset.core.scaleVelocity, 0.5f, -100.0f, 100.0f);
      changed |= ImGui::DragFloat((const char *)u8"寿命 (Life)", &preset.core.life, 0.01f, 0.02f, 2.0f);
      ImGui::TreePop();
    }

    if (ImGui::TreeNode((const char *)u8"散乱火花 (Flare Sparks)")) {
      changed |= ImGui::Checkbox((const char *)u8"有効 (Flare Enable)", &preset.flare.enable);
      changed |= ImGui::SliderInt((const char *)u8"発生粒数 (Count)", &preset.flare.count, 0, 80);
      changed |= ImGui::DragFloat((const char *)u8"粒サイズ (Scale)", &preset.flare.scale, 0.02f, 0.05f, 10.0f);
      changed |= ImGui::DragFloat((const char *)u8"飛散初速 (Speed)", &preset.flare.speed, 0.5f, 1.0f, 100.0f);
      changed |= ImGui::DragFloat((const char *)u8"最小寿命 (LifeMin)", &preset.flare.lifeMin, 0.01f, 0.02f, 2.0f);
      changed |= ImGui::DragFloat((const char *)u8"最大寿命 (LifeMax)", &preset.flare.lifeMax, 0.01f, 0.02f, 2.0f);
      ImGui::TreePop();
    }

    if (ImGui::TreeNode((const char *)u8"衝撃波リング (Shock Ring)")) {
      changed |= ImGui::Checkbox((const char *)u8"有効 (Ring Enable)", &preset.ring.enable);
      changed |= ImGui::SliderInt((const char *)u8"重なり枚数 (Count)", &preset.ring.count, 0, 5);
      changed |= ImGui::DragFloat((const char *)u8"初期半径 (Start Scale)", &preset.ring.startScale, 0.02f, 0.05f, 5.0f);
      changed |= ImGui::DragFloat((const char *)u8"拡散速度 (Expand Speed)", &preset.ring.expandSpeed, 0.5f, 1.0f, 200.0f);
      changed |= ImGui::DragFloat((const char *)u8"寿命 (Life)", &preset.ring.life, 0.01f, 0.02f, 2.0f);
      ImGui::TreePop();
    }

    if (changed) {
      isEffectsConfigDirty_ = true;
    }
  }

  ImGui::Separator();
  if (ImGui::TreeNode((const char *)u8"ショックウェーブ全体設定 (Shockwave Master)")) {
    bool changed = false;
    changed |= ImGui::DragFloat((const char *)u8"再生時間 (Duration)",
                                &shockwaveConfig_.duration, 0.01f, 0.1f, 5.0f);
    changed |= ImGui::DragFloat((const char *)u8"最大半径 (Max Radius)",
                                &shockwaveConfig_.maxRadius, 0.01f, 0.1f, 5.0f);
    changed |= ImGui::DragFloat((const char *)u8"歪みの強さ (Distortion)",
                                &shockwaveConfig_.distortion, 0.001f, 0.0f, 0.5f);
    changed |= ImGui::DragFloat((const char *)u8"波の太さ (Thickness)",
                                &shockwaveConfig_.thickness, 0.001f, 0.0f, 1.0f);

    if (changed) {
      isShockwaveConfigDirty_ = true;
    }
    ImGui::TreePop();
  }
#endif
}

void EffectManager::SaveShockwaveConfig() {
  nlohmann::json root;
  root["duration"] = shockwaveConfig_.duration;
  root["maxRadius"] = shockwaveConfig_.maxRadius;
  root["distortion"] = shockwaveConfig_.distortion;
  root["thickness"] = shockwaveConfig_.thickness;

  if (!std::filesystem::exists("resources/config")) {
    std::filesystem::create_directories("resources/config");
  }

  std::ofstream file("resources/config/ShockwaveConfig.json");
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
    isShockwaveConfigDirty_ = false;
  }
}

void EffectManager::LoadShockwaveConfig() {
  std::ifstream file("resources/config/ShockwaveConfig.json");
  if (file.is_open()) {
    nlohmann::json root;
    try {
      file >> root;
      if (root.contains("duration"))
        shockwaveConfig_.duration = root["duration"];
      if (root.contains("maxRadius"))
        shockwaveConfig_.maxRadius = root["maxRadius"];
      if (root.contains("distortion"))
        shockwaveConfig_.distortion = root["distortion"];
      if (root.contains("thickness"))
        shockwaveConfig_.thickness = root["thickness"];
    } catch (...) {
      // Parse error, keep defaults
    }
  }
  isShockwaveConfigDirty_ = false;
}

void EffectManager::SaveEffectsConfig() {
  nlohmann::json root;
  const char *keys[] = {"HitSpark", "HomingHit", "EnemyDeath", "EnemyDeathSimple", "MuzzleRing"};

  for (size_t i = 0; i < static_cast<size_t>(EffectType::Count); ++i) {
    const auto &preset = effectConfigs_[i];
    nlohmann::json item;

    item["enableShockwave"] = preset.enableShockwave;

    item["core"]["enable"] = preset.core.enable;
    item["core"]["scale"] = preset.core.scale;
    item["core"]["scaleVelocity"] = preset.core.scaleVelocity;
    item["core"]["life"] = preset.core.life;

    item["flare"]["enable"] = preset.flare.enable;
    item["flare"]["count"] = preset.flare.count;
    item["flare"]["scale"] = preset.flare.scale;
    item["flare"]["scaleVelocity"] = preset.flare.scaleVelocity;
    item["flare"]["speed"] = preset.flare.speed;
    item["flare"]["lifeMin"] = preset.flare.lifeMin;
    item["flare"]["lifeMax"] = preset.flare.lifeMax;

    item["ring"]["enable"] = preset.ring.enable;
    item["ring"]["count"] = preset.ring.count;
    item["ring"]["startScale"] = preset.ring.startScale;
    item["ring"]["expandSpeed"] = preset.ring.expandSpeed;
    item["ring"]["life"] = preset.ring.life;

    root[keys[i]] = item;
  }

  if (!std::filesystem::exists("resources/config")) {
    std::filesystem::create_directories("resources/config");
  }

  std::ofstream file("resources/config/EffectsConfig.json");
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
    isEffectsConfigDirty_ = false;
  }
}

void EffectManager::LoadEffectsConfig() {
  // デフォルト値設定
  // 1. HitSpark (通常弾着弾)
  {
    auto &p = effectConfigs_[static_cast<size_t>(EffectType::HitSpark)];
    p.enableShockwave = false;
    p.core = {true, 12.0f, -20.0f, 0.25f};
    p.flare = {true, 16, 1.2f, -1.0f, 22.0f, 0.25f, 0.40f};
    p.ring = {true, 1, 0.2f, 50.0f, 0.20f};
  }
  // 2. HomingHit (ホーミング弾着弾：太く鋭い十字グリント＋極少数の高速火花＋高速プラズマリング)
  {
    auto &p = effectConfigs_[static_cast<size_t>(EffectType::HomingHit)];
    p.enableShockwave = false;
    p.core = {true, 32.0f, -10.0f, 0.20f};                 // 0.20秒持続し、十字のシルエットが目に残る
    p.flare = {true, 8, 1.2f, -1.5f, 40.0f, 0.15f, 0.25f}; // 8本の高速火花
    p.ring = {true, 1, 0.50f, 90.0f, 0.16f};               // 0.16秒で特大拡散するリング
  }
  // 3. EnemyDeath
  {
    auto &p = effectConfigs_[static_cast<size_t>(EffectType::EnemyDeath)];
    p.enableShockwave = true;
    p.core = {true, 20.0f, -20.0f, 0.50f};
    p.flare = {true, 40, 0.8f, -1.0f, 30.0f, 0.40f, 0.60f};
    p.ring = {true, 1, 0.1f, 80.0f, 0.70f};
  }
  // 4. EnemyDeathSimple
  {
    auto &p = effectConfigs_[static_cast<size_t>(EffectType::EnemyDeathSimple)];
    p.enableShockwave = false;
    p.core = {true, 30.0f, -20.0f, 0.50f};
    p.flare = {true, 30, 2.5f, -1.0f, 20.0f, 0.40f, 0.60f};
    p.ring = {false, 0, 0.1f, 0.0f, 0.0f};
  }
  // 5. MuzzleRing
  {
    auto &p = effectConfigs_[static_cast<size_t>(EffectType::MuzzleRing)];
    p.enableShockwave = false;
    p.core = {false, 0.0f, 0.0f, 0.0f};
    p.flare = {false, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    p.ring = {true, 3, 0.2f, 15.0f, 0.50f};
  }

  // ファイルから上書きロード
  std::ifstream file("resources/config/EffectsConfig.json");
  if (file.is_open()) {
    nlohmann::json root;
    try {
      file >> root;
      const char *keys[] = {"HitSpark", "HomingHit", "EnemyDeath", "EnemyDeathSimple", "MuzzleRing"};

      for (size_t i = 0; i < static_cast<size_t>(EffectType::Count); ++i) {
        const char *key = keys[i];
        if (root.contains(key)) {
          auto &preset = effectConfigs_[i];
          const auto &item = root[key];

          if (item.contains("enableShockwave"))
            preset.enableShockwave = item["enableShockwave"];

          if (item.contains("core")) {
            const auto &c = item["core"];
            if (c.contains("enable")) preset.core.enable = c["enable"];
            if (c.contains("scale")) preset.core.scale = c["scale"];
            if (c.contains("scaleVelocity")) preset.core.scaleVelocity = c["scaleVelocity"];
            if (c.contains("life")) preset.core.life = c["life"];
          }

          if (item.contains("flare")) {
            const auto &f = item["flare"];
            if (f.contains("enable")) preset.flare.enable = f["enable"];
            if (f.contains("count")) preset.flare.count = f["count"];
            if (f.contains("scale")) preset.flare.scale = f["scale"];
            if (f.contains("scaleVelocity")) preset.flare.scaleVelocity = f["scaleVelocity"];
            if (f.contains("speed")) preset.flare.speed = f["speed"];
            if (f.contains("lifeMin")) preset.flare.lifeMin = f["lifeMin"];
            if (f.contains("lifeMax")) preset.flare.lifeMax = f["lifeMax"];
          }

          if (item.contains("ring")) {
            const auto &r = item["ring"];
            if (r.contains("enable")) preset.ring.enable = r["enable"];
            if (r.contains("count")) preset.ring.count = r["count"];
            if (r.contains("startScale")) preset.ring.startScale = r["startScale"];
            if (r.contains("expandSpeed")) preset.ring.expandSpeed = r["expandSpeed"];
            if (r.contains("life")) preset.ring.life = r["life"];
          }
        }
      }
    } catch (...) {
      // JSONパースエラー時はデフォルト設定を維持
    }
  }
  isEffectsConfigDirty_ = false;
}
