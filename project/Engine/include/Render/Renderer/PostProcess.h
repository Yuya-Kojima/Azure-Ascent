#pragma once
#include "Core/Dx12Core.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include <vector>
#include <wrl.h>

class SrvManager;

class PostProcess {
public:
  void Initialize(Dx12Core *dx12Core);
  void Draw(uint32_t renderSrvIndex, uint32_t depthSrvIndex,
            SrvManager *srvManager);

  /// <summary>
  /// ImGuiを使用したデバッグ用の設定UIを描画する
  /// </summary>
  void DrawDebugUI(const char *windowName = "PostEffect Settings",
                   bool createNewWindow = true);

  /// <summary>
  /// グレースケールの有効/無効を切り替える
  /// </summary>
  /// <param name="useGrayscale">trueで有効、falseで無効</param>
  void SetUseGrayscale(bool useGrayscale) { useGrayscale_ = useGrayscale; }

  /// <summary>
  /// グレースケールする際の色合い（セピア調など）を設定する
  /// </summary>
  /// <param name="r"></param>
  /// <param name="g"></param>
  /// <param name="b"></param>
  void SetMonotoneColor(float r, float g, float b) {
    monotoneColor_[0] = r;
    monotoneColor_[1] = g;
    monotoneColor_[2] = b;
  }

  /// <summary>
  /// ビネット（画面四隅を暗くする効果）の有効/無効を切り替える
  /// </summary>
  /// <param name="useVignette">trueで有効、falseで無効</param>
  void SetUseVignette(bool useVignette) { useVignette_ = useVignette; }

  /// <summary>
  /// ビネットのスケール（暗くなる範囲の強さ）を設定する
  /// </summary>
  /// <param name="scale">スケール値（大きいほど暗い領域が狭まる）</param>
  void SetVignetteScale(float scale) { vignetteScale_ = scale; }

  /// <summary>
  /// ビネットの減衰率を設定する
  /// </summary>
  /// <param name="exponent">減衰率（大きいほど境界がクッキリする）</param>
  void SetVignetteExponent(float exponent) { vignetteExponent_ = exponent; }

  /// <summary>
  /// ポストエフェクトの種類を切り替える (互換性のため残す)
  /// </summary>
  /// <param name="type">0: None, 1: BoxFilter, 2: GaussianFilter, etc.</param>
  void SetPostEffectType(int type) { postEffectType_ = type; }

  /// <summary>
  /// 現在のポストエフェクトのタイプを取得する
  /// </summary>
  int GetPostEffectType() const { return postEffectType_; }

  void SetUseDepthOutline(bool use) { useDepthOutline_ = use; }
  void SetUseRadialBlur(bool use) { useRadialBlur_ = use; }
  void SetUseDissolve(bool use) { useDissolve_ = use; }
  void SetUseGaussianFilter(bool use) { useGaussianFilter_ = use; }

  /// <summary>
  /// ボックスフィルターのカーネルサイズを設定する
  /// </summary>
  /// <param name="k">サンプリング半径（1で3x3、2で5x5）</param>
  void SetBoxFilterK(int k) { boxFilterK_ = k; }

  /// <summary>
  /// ガウシアンフィルターのカーネルサイズを設定する
  /// </summary>
  /// <param name="k">サンプリング半径（1で3x3、2で5x5）</param>
  void SetGaussianFilterK(int k) { gaussianFilterK_ = k; }

  /// <summary>
  /// ガウシアンフィルターの標準偏差（ぼかしの強さ）を設定する
  /// </summary>
  /// <param name="sigma">シグマ値（大きいほど強くぼやける）</param>
  void SetGaussianSigma(float sigma) { gaussianSigma_ = sigma; }

  /// <summary>
  /// アウトラインの太さ・重みを設定する
  /// </summary>
  /// <param name="weight">輪郭線の重み</param>
  void SetDepthOutlineWeight(float weight) { depthOutlineWeight_ = weight; }

  /// <summary>
  /// アウトラインの減衰率を設定する
  /// </summary>
  /// <param name="attenuation">距離による減衰度合い</param>
  void SetDepthOutlineAttenuation(float attenuation) {
    depthOutlineAttenuation_ = attenuation;
  }

  /// <summary>
  /// ラジアルブラーの中心座標を設定する
  /// </summary>
  /// <param name="x">画面内X座標（0.0～1.0）</param>
  /// <param name="y">画面内Y座標（0.0～1.0）</param>
  void SetRadialBlurCenter(float x, float y) {
    radialBlurCenter_[0] = x;
    radialBlurCenter_[1] = y;
  }

  /// <summary>
  /// ラジアルブラーの幅（ぼかしの広がり具合）を設定する
  /// </summary>
  /// <param name="width">ぼかしの強さ</param>
  void SetRadialBlurWidth(float width) { radialBlurWidth_ = width; }

  /// <summary>
  /// ラジアルブラーのサンプリング回数を設定する
  /// </summary>
  /// <param name="samples">サンプリング数（多いほど綺麗だが重い）</param>
  void SetRadialBlurSamples(int samples) { radialBlurSamples_ = samples; }

  /// <summary>
  /// ラジアルブラーがかからない内側の半径を設定する
  /// </summary>
  /// <param name="radius">内側の半径（0.0～1.0）</param>
  void SetRadialBlurInnerRadius(float radius) {
    radialBlurInnerRadius_ = radius;
  }

  /// <summary>
  /// ラジアルブラーがかかる外側の半径を設定する
  /// </summary>
  /// <param name="radius">外側の半径（0.0～1.0）</param>
  void SetRadialBlurOuterRadius(float radius) {
    radialBlurOuterRadius_ = radius;
  }

  /// <summary>
  /// ラジアルブラーの色収差（RGBのズレ）の強さを設定する
  /// </summary>
  /// <param name="aberration">色収差の強さ</param>
  void SetRadialBlurAberration(float aberration) {
    radialBlurAberration_ = aberration;
  }

  /// <summary>
  /// 画面全体ディゾルブの進行度を設定する
  /// </summary>
  /// <param name="threshold">0.0（完全表示）～ 1.0（完全消失）</param>
  void SetDissolveThreshold(float threshold) { dissolveThreshold_ = threshold; }

  /// <summary>
  /// 画面全体ディゾルブのエッジの太さを設定する
  /// </summary>
  /// <param name="range">エッジの太さ（例: 0.05）</param>
  void SetDissolveEdgeRange(float range) { dissolveEdgeRange_ = range; }

  /// <summary>
  /// 画面全体ディゾルブのエッジの色を設定する
  /// </summary>
  /// <param name="r">赤成分</param>
  /// <param name="g">緑成分</param>
  /// <param name="b">青成分</param>
  void SetDissolveEdgeColor(float r, float g, float b) {
    dissolveEdgeColor_[0] = r;
    dissolveEdgeColor_[1] = g;
    dissolveEdgeColor_[2] = b;
  }

  /// <summary>
  /// シェーダーに渡す経過時間（乱数シードなどのアニメーション用）を設定する
  /// </summary>
  /// <param name="time">経過時間</param>
  void SetTime(float time) { time_ = time; }

  /// <summary>
  /// プロジェクション行列の逆行列を設定する
  /// </summary>
  /// <param name="m">逆プロジェクション行列</param>
  void SetProjectionInverse(const Matrix4x4 &m) { projectionInverse_ = m; }

  /// <summary>
  /// Dissolveなどに使用するマスク画像のSRVインデックスを設定する
  /// </summary>
  /// <param name="index">SRVインデックス</param>
  void SetMaskSrvIndex(uint32_t index) { maskSrvIndex_ = index; }

  /// <summary>
  /// ブルーム用テクスチャのSRVインデックスを設定する
  /// </summary>
  void SetBloomSrvIndex(uint32_t index) { bloomSrvIndex_ = index; }

  /// <summary>
  /// ブルームの適用フラグを設定する
  /// </summary>
  void SetUseBloom(bool use) { useBloom_ = use; }

  /// <summary>
  /// ブルームの強度を設定する
  /// </summary>
  void SetBloomIntensity(float intensity) { bloomIntensity_ = intensity; }

  // Bloom詳細パラメータのGetter/Setter
  bool GetUseBloom() const { return useBloom_; }
  float GetBloomThreshold() const { return bloomThreshold_; }
  float GetBloomSigma() const { return bloomSigma_; }
  void SetBloomThreshold(float t) { bloomThreshold_ = t; }
  void SetBloomSigma(float s) { bloomSigma_ = s; }

  // Tone Mappingパラメータ
  int32_t GetToneMappingType() const { return toneMappingType_; }
  float GetExposure() const { return exposure_; }
  void SetToneMappingType(int32_t type) { toneMappingType_ = type; }
  void SetExposure(float exposure) { exposure_ = exposure; }

  void SetHistorySrvIndex(uint32_t index) { historySrvIndex_ = index; }
  void SetMotionBlurAlpha(float alpha) { motionBlurAlpha_ = alpha; }
  float GetMotionBlurAlpha() const { return motionBlurAlpha_; }

  // DoF (被写界深度) パラメータ
  void SetDofFocusDistance(float dist) { dofFocusDistance_ = dist; }
  float GetDofFocusDistance() const { return dofFocusDistance_; }
  void SetDofFocusRange(float range) { dofFocusRange_ = range; }
  float GetDofFocusRange() const { return dofFocusRange_; }
  void SetFlashColor(float r, float g, float b) {
    flashColor_[0] = r;
    flashColor_[1] = g;
    flashColor_[2] = b;
  }
  void SetFlashIntensity(float intensity) { flashIntensity_ = intensity; }

  /// <summary>
  /// HSVフィルターの色相(Hue)の加算値を設定する
  /// </summary>
  void SetHsvFilterHue(float hue) { hsvFilterHue_ = hue; }

  /// <summary>
  /// HSVフィルターの彩度(Saturation)の加算値を設定する
  /// </summary>
  void SetHsvFilterSaturation(float saturation) {
    hsvFilterSaturation_ = saturation;
  }

  /// <summary>
  /// HSVフィルターの明度(Value)の加算値を設定する
  /// </summary>
  void SetHsvFilterValue(float value) { hsvFilterValue_ = value; }

  struct ShockwaveParams {
    float weight = 1.0f;
    float distortion = 0.05f;
    float radius = 0.0f;
    float thickness = 0.1f;
    float center[2] = {0.5f, 0.5f};
    float aberration = 0.0f; // 色ずれ（押し曲げる量に対する赤・青のずらし量の差の割合）
  };
  void SetShockwaves(const std::vector<ShockwaveParams> &shockwaves) {
    shockwaves_ = shockwaves;
  }

private:
  Dx12Core *dx12Core_ = nullptr;
  int postEffectType_ = 0;
  bool useDepthOutline_ = false;
  bool useRadialBlur_ = false;
  bool useDissolve_ = false;
  bool useGaussianFilter_ = false;
  int boxFilterK_ = 1;
  int gaussianFilterK_ = 1;
  float gaussianSigma_ = 4.0f;
  struct PostProcessData {
    int32_t postEffectType;
    int32_t useDepthOutline;
    int32_t useRadialBlur;
    int32_t useDissolve;
    int32_t useGaussianFilter;
    int32_t useGrayscale;
    int32_t useVignette;
    int32_t boxFilterK;
    Vector3 monotoneColor; // グレースケールの色合い
    float vignetteScale;   // ヴィネットの範囲
    float vignetteExponent;
    int32_t gaussianFilterK;  // ガウシアンぼかしサイズ（サンプリング半径）
    float gaussianSigma;      // ぼかし強さ
    float depthOutlineWeight; // アウトラインの太さ
    float depthOutlineAttenuation;
    Vector3 flashColor; // 画面フラッシュの色
    Matrix4x4 projectionInverse;
    float radialBlurCenter[2]; // ラジアルブラーの中心座標
    float radialBlurWidth;     // ラジアルブラーの強さ
    int32_t radialBlurSamples;
    float radialBlurInnerRadius; // ラジアルブラーがかからない内側の半径
    float radialBlurOuterRadius; // ラジアルブラーがかかる外側の半径
    float radialBlurAberration;
    float padding4;
    float dissolveThreshold; // ディゾルブ (画面消滅) の進行度
    float dissolveEdgeRange;
    float padding5[2];
    Vector3 dissolveEdgeColor; // ディゾルブの境界線の発光色
    float time;
    float hsvFilterHue;        // HSV 色相の加算値
    float hsvFilterSaturation; // HSV 彩度の加算値
    float hsvFilterValue;      // HSV 明度の加算値
    float padding6;
    float bloomIntensity; // ブルームの強さ
    int32_t useBloom;
    int32_t
        toneMappingType; // トーンマッピングの種類 (0:なし, 1:ACES, 2:Reinhard)
    float exposure;      // 画面の露出
    float motionBlurAlpha;  // モーションブラーの強さ
    float dofFocusDistance; // 被写界深度 (DoF) のピントが合う距離
    float dofFocusRange;
    float flashIntensity; // 画面フラッシュの強さ
    int32_t activeShockwaveCount;
    float padding9[3];

    // ショックウェーブのデータ構造
    struct ShockwaveData {
      float weight;     // 衝撃波のゆがみの強さ
      float distortion; // 衝撃波のゆがみ幅
      float radius;     // 衝撃波の現在の半径
      float thickness;  // 衝撃波の波の太さ
      float center[2];  // 衝撃波の中心座標
      float aberration; // 衝撃波の色ずれ
      float padding;
    } shockwaves[5];
  };

  PostProcessData *mappedData_ = nullptr;
  bool useGrayscale_ = false;
  float monotoneColor_[3] = {1.0f, 1.0f, 1.0f};

  bool useVignette_ = false;
  float vignetteScale_ = 16.0f;
  float vignetteExponent_ = 0.8f;

  float depthOutlineWeight_ = 10.0f;
  float depthOutlineAttenuation_ = 0.05f;

  float radialBlurCenter_[2] = {0.5f, 0.5f};
  float radialBlurWidth_ = 0.01f;
  int32_t radialBlurSamples_ = 10;
  float radialBlurInnerRadius_ = 0.0f;
  float radialBlurOuterRadius_ = 0.5f;
  float radialBlurAberration_ = 0.1f;

  float dissolveThreshold_ = 0.5f;
  float dissolveEdgeRange_ = 0.05f;
  float dissolveEdgeColor_[3] = {1.0f, 0.4f, 0.3f};

  float hsvFilterHue_ = 0.0f;
  float hsvFilterSaturation_ = 0.0f;
  float hsvFilterValue_ = 0.0f;

  float time_ = 0.0f;

  uint32_t maskSrvIndex_ = 0;
  uint32_t bloomSrvIndex_ = 0;
  bool useBloom_ = false;
  float bloomIntensity_ = 1.0f;
  float bloomThreshold_ = 0.8f;
  float bloomSigma_ = 5.0f;

  int32_t toneMappingType_ = 1; // 0: None, 1: ACES Filmic, 2: Reinhard
  float exposure_ = 0.5f;

  uint32_t historySrvIndex_ = 0;
  float motionBlurAlpha_ = 0.0f; // 0.0でオフ

  float dofFocusDistance_ = 10.0f;
  float dofFocusRange_ = 5.0f;
  float flashColor_[3] = {1.0f, 0.0f, 0.0f}; // 画面フラッシュ用カラー
  float flashIntensity_ = 0.0f;              // 画面フラッシュ強度

  std::vector<ShockwaveParams> shockwaves_;

  Matrix4x4 projectionInverse_;

  Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;

  void CreateRootSignature();
  void CreatePSO();
};
