#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include <vector>
#include <memory>
#include <array>
#include "Render/Particle/ParticleEmitter.h"
#include "Render/Particle/BillboardParticleEmitter.h"

class RailCamera;
class ICamera;

enum class EffectType {
  HitSpark,          // 着弾ヒットスパーク（通常弾・実弾用）
  HomingHit,         // ホーミング専用着弾（太く鋭い十字グリント＋高速プラズマリング）
  EnemyDeath,        // 敵撃破（大爆発＋ショックウェーブ＋火花散乱）
  EnemyDeathSimple,  // 敵撃破・中（コア＋火花）
  MuzzleRing,        // 急発進・マズルリング（3重リング）
  Count
};

struct EffectCoreConfig {
  bool enable = true;
  float scale = 3.5f;
  float scaleVelocity = -30.0f;
  float life = 0.10f;
};

struct EffectFlareConfig {
  bool enable = true;
  int count = 16;
  float scale = 0.35f;
  float scaleVelocity = -0.8f;
  float speed = 18.0f;
  float lifeMin = 0.12f;
  float lifeMax = 0.22f;
};

struct EffectRingConfig {
  bool enable = true;
  int count = 1;
  float startScale = 0.1f;
  float expandSpeed = 45.0f;
  float life = 0.10f;
};

struct EffectPresetConfig {
  EffectCoreConfig core;
  EffectFlareConfig flare;
  EffectRingConfig ring;
  bool enableShockwave = false;
};

class EffectManager {
public:
  static EffectManager *GetInstance();

  /// <summary>
  /// 初期化（設定を読み、残っているエフェクトを消す。粒の入れ物は最初の1回だけ作る）
  /// </summary>
  void Initialize();

  /// <summary>
  /// 終了（粒の入れ物を解放する。デバイスを消す前に呼ぶ）
  /// </summary>
  void Finalize();

  void Update(const ICamera* camera);
  void Draw();

  /// <summary>
  /// エディタ上のUI描画（テスト再生用カメラ情報を受け取る）
  /// </summary>
  void DrawEditorUI(RailCamera *railCamera);

  /// <summary>
  /// 汎用エフェクト再生インターフェース
  /// </summary>
  void PlayEffect(EffectType type, const Vector3 &worldPos,
                  const Vector4 &color = {1.0f, 1.0f, 1.0f, 1.0f},
                  float scaleMultiplier = 1.0f);

  /// <summary>
  /// ショックウェーブ（波紋）を発生させる
  /// </summary>
  void PlayShockwave(const Vector3 &worldPos);

  /// <summary>
  /// 汎用的な敵の撃破エフェクト（後方互換ラッパー）
  /// </summary>
  void PlayEnemyDeathEffect(const Vector3 &worldPos, const Vector4 &baseColor = {1.0f, 1.0f, 1.0f, 1.0f}) {
    PlayEffect(EffectType::EnemyDeath, worldPos, baseColor);
  }

  /// <summary>
  /// ボスの予兆エフェクト（収束するエネルギー）を発生させる
  /// </summary>
  void PlayBossTelegraphEffect(const Vector3& center, const Vector3& targetPos, float chargeRatio, int attackPattern);

  /// <summary>
  /// ボスの発射時エフェクト（発散するエネルギーと衝撃波）を発生させる
  /// </summary>
  void PlayBossBurstEffect(const Vector3& center);

  /// <summary>
  /// ミサイル急発進時の白いリングエフェクト（後方互換ラッパー）
  /// </summary>
  void PlayFunnelMuzzleRing(const Vector3 &worldPos, const Vector4 &color = {1.0f, 1.0f, 1.0f, 1.0f}) {
    PlayEffect(EffectType::MuzzleRing, worldPos, color);
  }

  /// <summary>
  /// ザコ敵用のシンプルな撃破エフェクト（後方互換ラッパー）
  /// </summary>
  void PlayEnemyDeathSimpleEffect(const Vector3 &worldPos, const Vector4 &baseColor = {1.0f, 1.0f, 1.0f, 1.0f}) {
    PlayEffect(EffectType::EnemyDeathSimple, worldPos, baseColor);
  }

private:
  EffectManager() = default;
  ~EffectManager() = default;
  EffectManager(const EffectManager &) = delete;
  EffectManager &operator=(const EffectManager &) = delete;

  std::unique_ptr<BillboardParticleEmitter> bossTelegraphNormalParticleGroup_;
  std::unique_ptr<ParticleEmitter> bossTelegraphNormalEmitter_;


  std::unique_ptr<BillboardParticleEmitter> bossBurstParticleGroup_;
  std::unique_ptr<ParticleEmitter> bossBurstEmitter_;

  struct ShockwaveConfig {
    float duration = 0.5f;    // 再生時間
    float maxRadius = 0.8f;   // 最大半径
    float distortion = 0.05f; // 歪みの強さ
    float thickness = 0.1f;   // 波の太さ
  };
  ShockwaveConfig shockwaveConfig_;
  bool isShockwaveConfigDirty_ = false;

  struct ActiveShockwave {
    float timer;
    Vector3 worldPos;
  };
  std::vector<ActiveShockwave> activeShockwaves_;


  void SaveShockwaveConfig();
  void LoadShockwaveConfig();

  void SaveEffectsConfig();
  void LoadEffectsConfig();

  std::array<EffectPresetConfig, static_cast<size_t>(EffectType::Count)> effectConfigs_;
  bool isEffectsConfigDirty_ = false;
  int selectedEffectIndex_ = 0;

  static const int kMaxHitEffects = 32;
  std::array<std::unique_ptr<BillboardParticleEmitter>, kMaxHitEffects> hitCoreParticleGroups_;
  std::array<std::unique_ptr<BillboardParticleEmitter>, kMaxHitEffects> hitFlareParticleGroups_;
  std::array<std::unique_ptr<BillboardParticleEmitter>, kMaxHitEffects> hitRingParticleGroups_;
  std::array<std::unique_ptr<BillboardParticleEmitter>, kMaxHitEffects> hitGlintParticleGroups_;

  std::array<std::unique_ptr<ParticleEmitter>, kMaxHitEffects> deathCoreEmitters_;
  std::array<std::unique_ptr<ParticleEmitter>, kMaxHitEffects> deathFlareEmitters_;
  std::array<std::unique_ptr<ParticleEmitter>, kMaxHitEffects> deathRingEmitters_;
  std::array<std::unique_ptr<ParticleEmitter>, kMaxHitEffects> hitGlintEmitters_;
  int nextHitEffectIndex_ = 0;

  // 粒の入れ物（BillboardParticleEmitter）を作ったか
  bool hasCreatedParticleGroups_ = false;

  /// <summary>
  /// 粒の入れ物（GPU バッファ）を作る
  /// </summary>
  void CreateParticleGroups();

  /// <summary>
  /// すべての粒の入れ物に残っている粒を消す
  /// </summary>
  void ClearParticleGroups();
};
