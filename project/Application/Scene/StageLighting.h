#pragma once
#include "Math/Vector3.h"

class Skybox;
class Object3dRenderer;
class PostProcess;

/// <summary>
/// ステージの空・光・霧・ポストエフェクトの値を持ち、毎フレーム描画側へ設定する。
/// 値は resources/config/StageLightingConfig.json に保存し、ImGui から調整する
/// </summary>
class StageLighting {
public:
  // 画面の色は「線形の値 × 露出 → ACES トーンマップ → sRGB」で決まる
  struct Config {
    // 空（手続き生成）
    Vector3 skyZenith = {0.025f, 0.025f, 0.035f};    // 天頂の色
    Vector3 skyHorizon = {0.072f, 0.080f, 0.103f};   // 地平線の色
    Vector3 sunDir = {0.1f, -0.02f, 1.0f};           // 太陽の方向（y<0 で地平線の下に隠れる）
    Vector3 sunColor = {1.0f, 0.5f, 0.22f};          // 太陽の色
    float sunIntensity = 0.5f;                       // 太陽の本体と周りのにじみ、雲が太陽の色を受ける強さ
    float horizonGlow = 4.0f;                        // 地平線の光の強さ（太陽の色に掛ける）
    float horizonGlowHeight = 0.03f;                 // 地平線の光の帯の高さ（方向ベクトルの y）
    float horizonGlowAzimuth = 6.0f;                 // 地平線の光の方位の絞り（大きいほど太陽の左右だけ）
    float cloudAmount = 0.75f;                       // 雲の量（0〜1）
    Vector3 cloudColor = {0.025f, 0.026f, 0.034f};   // 雲のふたの色
    float cloudOpacity = 0.95f;                      // 雲の不透明度
    float cloudRim = 0.1f;                           // 太陽側で雲の縁が光る強さ

    // 平行光源と環境光
    Vector3 lightFrom = {0.5f, 0.65f, -0.55f};       // 光が来る方向（物体から光源へ向かう向き）
    Vector3 lightColor = {0.85f, 0.95f, 1.1f};       // 光の色
    float lightIntensity = 0.75f;                    // 光の強さ
    Vector3 skyAmbient = {0.35f, 0.40f, 0.50f};      // 上を向いた面が受ける環境光
    Vector3 groundAmbient = {0.18f, 0.20f, 0.25f};   // 下を向いた面が受ける環境光
    Vector3 backRimFrom = {0.1f, 0.05f, 1.0f};       // 逆光の縁を作る光が来る方向
    Vector3 backRimColor = {1.2f, 0.62f, 0.25f};     // 逆光の縁の色
    float backRimIntensity = 0.0f;                   // 逆光の縁の強さ（0で無効）
    float backRimPower = 4.0f;                       // 逆光の縁の鋭さ（大きいほど細い）

    // 霧（地平線の色に溶かす）
    float fogNear = 120.0f;                          // 霧がかかり始める距離
    float fogFar = 700.0f;                           // 地平線の色になりきる距離

    // ポストエフェクト
    float exposure = 0.6f;                           // 露出
    bool useBloom = true;                            // ブルームを使うか
    float bloomThreshold = 1.0f;                     // にじませる明るさ（露出前の輝度）
    float bloomIntensity = 0.4f;                     // にじみの強さ
    float bloomSigma = 4.0f;                         // にじみの広さ
    bool useVignette = true;                         // 周辺を暗くするか
    float vignetteScale = 16.0f;                     // 周辺減光の範囲（大きいほど暗い所が狭い）
    float vignetteExponent = 0.5f;                   // 周辺減光の境目の強さ
  };

  /// <summary>
  /// 設定ファイルを読み込む（ないときは既定値のまま）
  /// </summary>
  void Initialize();

  /// <summary>
  /// 空・光・霧・ポストエフェクトを設定する
  /// </summary>
  void Apply(Skybox *skybox, Object3dRenderer *renderer,
             PostProcess *postProcess, float skyTime) const;

  /// <summary>
  /// 全シーン共通の平行光源から、このクラスが足した環境光と逆光の縁を外す
  /// </summary>
  static void ResetSharedLight(Object3dRenderer *renderer);

  /// <summary>
  /// 調整用の ImGui（呼び出し側の窓の中に描く）
  /// </summary>
  void DrawImGui();

private:
  void LoadConfig();
  void SaveConfig() const;

  Config config_;
};
