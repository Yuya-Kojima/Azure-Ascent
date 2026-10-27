#pragma once
#include "Math/MathUtil.h"
#include <memory>
#include <string>

class CloudSea;
class CloudSeaRenderer;
class ICamera;
class Object3d;
class Object3dRenderer;
class PostProcess;
struct FogData;

/// <summary>
/// ゲームオーバーで拠点へ戻ったときの空と光
/// 空を覆う分厚い雲の天井（雲海を下から見る）と、ドラゴンをカメラ側の右斜め後ろから照らす光
/// 光の向きは拠点のドラゴンの向き（水平な右・上・前）を基準に決める
/// </summary>
class StageSelectReturnSky {
public:
  StageSelectReturnSky();
  ~StageSelectReturnSky();

  /// <summary>
  /// 初期化（雲海を作り、調整値を読む）
  /// </summary>
  void Initialize(CloudSeaRenderer *cloudSeaRenderer);

  /// <summary>
  /// 雲の天井の更新
  /// </summary>
  /// <param name="camera">描画に使うカメラ</param>
  /// <param name="deltaTime">進める時間（秒）</param>
  void Update(const ICamera *camera, float deltaTime);

  /// <summary>
  /// 光・霧・露出を、いつもの拠点の値から戻ったときの値へ weight の分だけ寄せる
  /// いつもの拠点の値を設定したあとに呼ぶ
  /// </summary>
  /// <param name="renderer">平行光源と霧を持つ描画</param>
  /// <param name="fog">いつもの拠点の霧（距離をここで書き換えて renderer へ渡す）</param>
  /// <param name="postProcess">露出を変える後処理</param>
  /// <param name="baseExposure">いつもの拠点の露出</param>
  /// <param name="dragonYaw">拠点のドラゴンの向き（Y 軸まわりの角度）</param>
  /// <param name="weight">0: いつもの拠点 〜 1: 戻ったとき</param>
  void ApplyLighting(Object3dRenderer *renderer, FogData fog,
                     PostProcess *postProcess, float baseExposure,
                     float dragonYaw, float weight) const;

  /// <summary>
  /// 地面の鏡面反射とドラゴンの縁の光を、戻ったときの値へ weight の分だけ寄せる
  /// </summary>
  void ApplyMaterials(Object3d *ground, Object3d *dragon, float weight) const;

  /// <summary>
  /// 雲の天井の描画（不透明な物を描いたあとに呼ぶ）
  /// </summary>
  void Draw();

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

private:
  // 光の調整値。向きはドラゴンの水平な右・上・前で表した「光が来る向き」
  struct Lighting {
    Vector3 lightFrom = {0.6f, 0.7f, -0.5f};      // 主の光が来る向き（右斜め後ろの上）
    Vector3 lightColor = {0.85f, 0.9f, 1.0f};     // 主の光の色
    float lightIntensity = 1.0f;                  // 主の光の強さ
    Vector3 skyAmbient = {0.16f, 0.18f, 0.24f};   // 上を向いた面が受ける環境光
    Vector3 groundAmbient = {0.05f, 0.06f, 0.05f}; // 下を向いた面が受ける環境光
    Vector3 rimFrom = {0.0f, 1.0f, 0.3f};         // 縁の光が来る向き（雲の天井から）
    Vector3 rimColor = {0.5f, 0.55f, 0.7f};       // 縁の光の色
    float rimIntensity = 0.6f;                    // 縁の光の強さ
    float rimPower = 3.0f;                        // 縁の光の鋭さ
    float dragonRimScale = 1.0f; // ドラゴンが縁の光を受ける倍率
    float groundSpecular = 0.0f; // 地面の鏡面反射の倍率
    float fogNear = 40.0f;       // 霧が始まる距離
    float fogFar = 300.0f;       // 霧で色が消えきる距離
    float exposure = 0.5f;       // 露出
  };

  // 雲の天井の調整値（雲海の値。高さはワールドの Y）
  struct Ceiling {
    float height = 30.0f;     // 上の面の基準の高さ
    float thickness = 10.0f;  // 層の厚み（上の面から下へ）
    float amplitude = 3.0f;   // 上の面の起伏の高さ
    float blobSize = 30.0f;   // 雲の塊の大きさ
    float edgeSoftness = 0.8f;
    float density = 1.2f;
    Vector3 color = {0.16f, 0.16f, 0.17f}; // 雲の色（光を受けて返す割合）
    float brightness = 2.0f;               // 雲が返す光の強さの倍率
    float powder = 1.0f;
    float valleyDarkness = 0.3f;
    float noiseSize = 40.0f;
    float detailSize = 6.0f;
    float detailStrength = 0.35f;
    Vector2 wind = {0.3f, -1.5f}; // 雲が流れる速さ（XZ、1 秒あたり）
  };

  // 調整値をキーと一緒に1つずつ visitor へ渡す（読み書きで同じ並びを使う）
  template <class Visitor> void VisitConfigValues(Visitor &&visit);
  void LoadConfig();
  void SaveConfig();

  std::unique_ptr<CloudSea> cloudSea_;
  float time_ = 0.0f; // 雲を流す時刻（秒）
  Lighting lighting_;
  Ceiling ceiling_;
};
