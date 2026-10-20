#pragma once
#include "Math/Vector3.h"
#include <optional>
#include <vector>

class ICamera;
class PostProcess;

/// <summary>
/// 咆哮の空間のゆがみ。吠え声からの秒に合わせて、頭を中心に広がる衝撃波の輪を EffectManager に渡し、
/// 放射状のぼかしを PostProcess に設定する。
/// 値は resources/config/RoarDistortionConfig.json に保存し、ImGui から調整する
/// </summary>
class RoarDistortion {
public:
  // 衝撃波の輪 1 本（長さは画面の高さを 1 とした値）
  struct Ring {
    float startTime = 0.15f;   // 出る時刻（吠え声からの秒）
    float duration = 0.65f;    // 出てから消えるまでの秒
    float maxRadius = 1.15f;   // 最大の半径
    float thickness = 0.08f;   // 太さの半分
    float distortion = 0.035f; // 押し曲げる量の最大
    float aberration = 0.3f;   // 色ずれ（押し曲げる量に対する赤・青のずらし量の差の割合）
  };

  // 放射状のぼかし（時刻は吠え声からの秒、半径は画面の UV での頭からの距離）
  struct Blur {
    float riseStartTime = 0.10f; // 強まり始める時刻
    float peakStartTime = 0.20f; // 最大になる時刻
    float peakEndTime = 0.40f;   // 弱まり始める時刻
    float endTime = 0.75f;       // 0 になる時刻
    float width = 0.008f;        // 最大のときのぼかしの幅
    int samples = 10;            // サンプル数
    float innerRadius = 0.35f;   // これより内側はぼかさない
    float outerRadius = 0.90f;   // これより外側は最大でぼかす
    float aberration = 0.15f;    // 色ずれ
  };

  struct Config {
    std::vector<Ring> rings;
    Blur blur;
  };

  /// <summary>
  /// 設定ファイルを読み込む（ないときは既定値）
  /// </summary>
  void Initialize();

  /// <summary>
  /// 今出ている輪を EffectManager に渡し、ぼかしを PostProcess に設定する。毎フレーム、EffectManager の Update より前に呼ぶ
  /// </summary>
  /// <param name="timeSinceRoar">吠え声からの秒（鳴る前は負）。開始演出が終わっているときは std::nullopt</param>
  /// <param name="headWorldPos">頭のワールド座標（輪とぼかしの中心）</param>
  /// <param name="camera">今映しているカメラ</param>
  /// <param name="postProcess">ぼかしを設定する先</param>
  void Update(std::optional<float> timeSinceRoar, const Vector3 &headWorldPos,
              const ICamera *camera, PostProcess *postProcess);

  /// <summary>
  /// 調整用の ImGui（呼び出し側の窓の中に「Roar Distortion」の項目として描く）
  /// </summary>
  void DrawImGui();

private:
  void LoadConfig();
  void SaveConfig() const;

  // 輪の強さ（0〜1）。出てすぐ最大になり、残り時間の 2 乗で弱まる。u は出てからの割合（0〜1）
  static float RingWeight(float u);
  // 輪の半径の割合（0〜1）。はじめ速く、だんだん遅く広がる
  static float RingRadiusRatio(float u);
  // ぼかしの強さ（0〜1）
  float BlurStrength(float time) const;

  Config config_;

  // 「ゆがみだけ再生」の経過秒（再生していないときは std::nullopt）
  std::optional<float> previewTime_;
  // ぼかしを PostProcess に入れているか（切るときに 1 回だけ戻す）
  bool isBlurActive_ = false;
};
