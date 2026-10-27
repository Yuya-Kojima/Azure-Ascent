#pragma once
#include "Math/MathUtil.h"
#include <cstdint>
#include <string>
#include <vector>

class CloudSea;
class ICamera;
class Object3dRenderer;
class PostProcess;

/// <summary>
/// ゲームオーバーで拠点へ戻ったときの、雲の天井の赤い雷
/// 決めた時刻に大きな雷が 1 回（雲の中が赤く光り、雲の底を横に這う稲妻）。
/// そのあとは間を空けて、雲の奥の毎回違う場所が弱く赤く光り、短い稲妻が走る。
/// 雷の明るさに合わせて、ドラゴンを上から赤く照らす。
/// 場所はすべて、拠点のドラゴンの位置と向き（水平な右・前）を基準に決める
/// </summary>
class StageSelectLightning {
public:
  /// <summary>
  /// 初期化（稲妻の帯に貼る画像を作り、稲妻の形を作る）
  /// </summary>
  void Initialize();

  /// <summary>
  /// 時刻から雷の明るさと場所を求める
  /// </summary>
  /// <param name="time">拠点に入ってからの時間（秒）</param>
  /// <param name="dragonPos">拠点で立っているときのドラゴンの位置</param>
  /// <param name="dragonYaw">拠点のドラゴンの向き（Y 軸まわりの角度）</param>
  /// <param name="ceilingBottom">雲の天井の底の高さ（ワールドの Y）</param>
  void Update(float time, const Vector3 &dragonPos, float dragonYaw,
              float ceilingBottom);

  /// <summary>
  /// 雲の中の光を雲海へ渡す
  /// </summary>
  void ApplyGlows(CloudSea *cloudSea) const;

  /// <summary>
  /// 雷の明るさで、ドラゴンの縁の光を赤く強め、上から赤い点の光で照らす
  /// いつもの光と縁の光を設定したあとに呼ぶ
  /// </summary>
  /// <param name="weight">0: 雷なし 〜 1: 戻ったとき</param>
  void ApplyLight(Object3dRenderer *renderer, float weight) const;

  /// <summary>
  /// 稲妻をにじませる Bloom の設定
  /// </summary>
  void ApplyPostProcess(PostProcess *postProcess) const;

  /// <summary>
  /// 稲妻の描画（雲の天井を描いたあとに呼ぶ）
  /// </summary>
  void Draw(const ICamera *camera);

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// 調整値をキーと一緒に1つずつ visitor へ渡す（読み書きで同じ並びを使う）
  /// </summary>
  template <class Visitor> void VisitConfigValues(Visitor &&visit);

  /// <summary>
  /// 調整値を読んだあとに呼ぶ（稲妻の形を作り直す）
  /// </summary>
  void OnConfigLoaded() { GenerateStrikeBolt(); }

private:
  // 稲妻の 1 本（幹か枝）。点は稲妻の中心からの（幹の向き, 上, 横）の位置
  struct BoltPath {
    std::vector<Vector3> points;
    float width = 1.0f;      // 幹に対する太さの割合
    float brightness = 1.0f; // 幹に対する明るさの割合
    bool isBranch = false;   // 枝は根元から先へ細くなる。幹は両端が細い
  };

  // 雲の中の光 1 つ
  struct Glow {
    Vector3 position{};
    float radius = 1.0f;
    Vector3 color{};
  };

  // 稲妻の置き場所と明るさ（ワールド）
  struct BoltPlacement {
    Vector3 center{};      // 稲妻の中心
    Vector3 axis{};        // 幹の向き（水平）
    Vector3 side{};        // 幹の横の向き（水平）
    float brightness = 0.0f; // 線の明るさ（0〜1）
    float width = 1.0f;      // 大きな雷に対する太さの割合
  };

  // 幹の長さ length・枝の数 branchCount の稲妻の形を作る（種から決まるので、同じ値なら毎回同じ形）
  std::vector<BoltPath> GenerateBolt(uint32_t seed, float length,
                                     int branchCount) const;
  // 大きな雷の稲妻の形を作り直す
  void GenerateStrikeBolt();
  // 稲妻 1 つを帯にして TrailRenderer へ登録する
  void AddBoltTrails(const std::vector<BoltPath> &paths,
                     const BoltPlacement &placement,
                     const Vector3 &cameraPos) const;

  // ドラゴンの位置から、水平な右・前で offset（x: 右, y: 前）だけ動かした XZ と、高さ y の位置
  Vector3 ToWorld(const Vector2 &offset, float y) const;

  //---- 時刻 ----
  float strikeTime_ = 3.8f;     // 大きな雷の時刻（秒、拠点に入ってから。カメラが止まったあと）
  float strikeAttackSec_ = 0.03f; // 光り始めてから最も明るくなるまでの時間（秒）
  float strikeHoldSec_ = 0.2f;  // 最も明るいままの時間（秒）
  float strikeFadeSec_ = 0.6f;  // 消えきるまでの時間（秒）
  float boltFadePower_ = 3.0f;  // 稲妻の線の明るさ＝雷の明るさのこの乗（大きいほど線が先に消え、雲の残光が残る）
  float glimmerTime_ = 1.8f;    // 降りてくる途中の、遠い弱い光の時刻（秒）
  float weakIntervalMin_ = 2.5f; // 大きな雷のあと、弱い光の間隔の最小（秒）
  float weakIntervalMax_ = 4.5f; // 弱い光の間隔の最大（秒）
  float weakRiseSec_ = 0.08f;   // 弱い光が最も明るくなるまでの時間（秒）
  float weakFadeSec_ = 0.7f;    // 弱い光が消えきるまでの時間（秒）

  //---- 場所（ドラゴンの右・前、雲の天井の底からの高さ）----
  Vector2 strikeOffset_ = {-12.0f, 75.0f}; // 大きな雷の中心（ドラゴンの視線の先、画面の右上）
  float glowDepth_ = 3.0f;    // 雲の中の光の中心を、雲の底からどれだけ上に置くか
  float boltLift_ = 1.0f;     // 稲妻を、雲の底からどれだけ上に置くか
  float weakAzimuthMin_ = -36.0f; // 弱い光の向きの範囲（度、ドラゴンの前から右が正）
  float weakAzimuthMax_ = 0.0f;
  float weakDistanceMin_ = 70.0f; // 弱い光のドラゴンからの水平な距離の範囲
  float weakDistanceMax_ = 170.0f;
  float weakMinSeparation_ = 10.0f; // 続く 2 回の弱い光の向きの差の最小（度）

  //---- 雲の中の光 ----
  Vector3 glowColor_ = {1.0f, 0.1f, 0.2f}; // 雷の赤（ボスの核と同じ系統）
  float strikeGlowIntensity_ = 3.0f;
  float strikeGlowRadius_ = 28.0f;
  float weakGlowIntensity_ = 0.9f;
  float weakGlowRadius_ = 20.0f;

  //---- 稲妻の形 ----
  float boltYaw_ = 75.0f;        // 幹の向き（度、ドラゴンの前から右が正）
  float boltLength_ = 38.0f;     // 幹の長さ
  float boltJaggedness_ = 0.35f; // 区間を割るとき、区間の長さに対して横へずらす量
  float boltVertical_ = 0.3f;    // 横へのずれに対する上下のずれの割合
  float branchCount_ = 3.0f;     // 枝の数
  float branchLengthMin_ = 0.25f; // 枝の長さ（幹に対する割合）
  float branchLengthMax_ = 0.45f;
  float branchAngleMin_ = 25.0f; // 枝が幹から開く角度（度）
  float branchAngleMax_ = 50.0f;
  float branchWidth_ = 0.5f;      // 枝の太さ（幹に対する割合）
  float branchBrightness_ = 0.6f; // 枝の明るさ（幹に対する割合）
  float seed_ = 7.0f;             // 形を決める種

  //---- 弱い光の稲妻 ----
  float weakBoltLength_ = 14.0f;     // 幹の長さ
  float weakBoltBranchCount_ = 1.0f; // 枝の数
  float weakBoltBrightness_ = 0.5f;  // 明るさ（大きな雷に対する割合）
  float weakBoltWidth_ = 0.7f;       // 太さ（大きな雷に対する割合）
  float weakBoltYawJitter_ = 20.0f;  // 幹の向きのばらつき（度、ドラゴンから見て横向きが基準）

  //---- 稲妻の見た目（露出前の明るさ）----
  Vector3 coreColor_ = {1.0f, 0.9f, 0.92f}; // 芯の色（白に近い）
  float coreIntensity_ = 10.0f;
  float coreWidth_ = 0.25f;
  float haloIntensity_ = 3.0f; // まわりのにじみ（雷の赤）の強さ
  float haloWidth_ = 1.6f;

  //---- ドラゴンへの光 ----
  float rimIntensity_ = 1.5f;   // 縁の光に足す赤の強さ（雷の明るさ 1 のとき）
  float dragonLightIntensity_ = 2.0f; // 上から照らす赤い点の光の強さ
  Vector3 dragonLightOffset_ = {-2.0f, 9.0f, 6.0f}; // 点の光の位置（ドラゴンの右・上・前）
  float dragonLightRadius_ = 20.0f; // 点の光が届く距離
  float dragonLightDecay_ = 1.0f;   // 点の光の弱まり方
  float weakDragonLight_ = 0.25f;   // 弱い光がドラゴンに届く割合（大きな雷を 1 として）

  //---- Bloom ----
  float bloomThreshold_ = 1.5f; // にじませる明るさ（露出前の輝度）
  float bloomIntensity_ = 1.0f;
  float bloomSigma_ = 4.0f;

  //---- 今の状態 ----
  Vector3 dragonPos_{};
  Vector3 forward_ = {0.0f, 0.0f, 1.0f};
  Vector3 right_ = {1.0f, 0.0f, 0.0f};
  std::vector<BoltPath> strikeBoltPaths_;
  std::vector<BoltPath> weakBoltPaths_;
  uint32_t weakBoltIndex_ = UINT32_MAX; // weakBoltPaths_ が何回目の弱い光の形か
  std::vector<Glow> glows_;
  float flash_ = 0.0f;           // ドラゴンへ届く雷の明るさ（大きな雷の最も明るいときが 1）
  BoltPlacement strikeBolt_;
  BoltPlacement weakBolt_;
};

template <class Visitor>
void StageSelectLightning::VisitConfigValues(Visitor &&visit) {
  auto visitVector2 = [&](const std::string &key, Vector2 &v) {
    visit(key + ".x", v.x);
    visit(key + ".y", v.y);
  };
  auto visitVector3 = [&](const std::string &key, Vector3 &v) {
    visit(key + ".x", v.x);
    visit(key + ".y", v.y);
    visit(key + ".z", v.z);
  };
  visit("time.strikeTime", strikeTime_);
  visit("time.strikeAttackSec", strikeAttackSec_);
  visit("time.strikeHoldSec", strikeHoldSec_);
  visit("time.strikeFadeSec", strikeFadeSec_);
  visit("time.boltFadePower", boltFadePower_);
  visit("time.glimmerTime", glimmerTime_);
  visit("time.weakIntervalMin", weakIntervalMin_);
  visit("time.weakIntervalMax", weakIntervalMax_);
  visit("time.weakRiseSec", weakRiseSec_);
  visit("time.weakFadeSec", weakFadeSec_);

  visitVector2("place.strikeOffset", strikeOffset_);
  visit("place.glowDepth", glowDepth_);
  visit("place.boltLift", boltLift_);
  visit("place.weakAzimuthMin", weakAzimuthMin_);
  visit("place.weakAzimuthMax", weakAzimuthMax_);
  visit("place.weakDistanceMin", weakDistanceMin_);
  visit("place.weakDistanceMax", weakDistanceMax_);
  visit("place.weakMinSeparation", weakMinSeparation_);

  visitVector3("glow.color", glowColor_);
  visit("glow.strikeIntensity", strikeGlowIntensity_);
  visit("glow.strikeRadius", strikeGlowRadius_);
  visit("glow.weakIntensity", weakGlowIntensity_);
  visit("glow.weakRadius", weakGlowRadius_);

  visit("bolt.yaw", boltYaw_);
  visit("bolt.length", boltLength_);
  visit("bolt.jaggedness", boltJaggedness_);
  visit("bolt.vertical", boltVertical_);
  visit("bolt.branchCount", branchCount_);
  visit("bolt.branchLengthMin", branchLengthMin_);
  visit("bolt.branchLengthMax", branchLengthMax_);
  visit("bolt.branchAngleMin", branchAngleMin_);
  visit("bolt.branchAngleMax", branchAngleMax_);
  visit("bolt.branchWidth", branchWidth_);
  visit("bolt.branchBrightness", branchBrightness_);
  visit("bolt.seed", seed_);
  visitVector3("bolt.coreColor", coreColor_);
  visit("bolt.coreIntensity", coreIntensity_);
  visit("bolt.coreWidth", coreWidth_);
  visit("bolt.haloIntensity", haloIntensity_);
  visit("bolt.haloWidth", haloWidth_);

  visit("weakBolt.length", weakBoltLength_);
  visit("weakBolt.branchCount", weakBoltBranchCount_);
  visit("weakBolt.brightness", weakBoltBrightness_);
  visit("weakBolt.width", weakBoltWidth_);
  visit("weakBolt.yawJitter", weakBoltYawJitter_);

  visit("dragon.rimIntensity", rimIntensity_);
  visit("dragon.lightIntensity", dragonLightIntensity_);
  visitVector3("dragon.lightOffset", dragonLightOffset_);
  visit("dragon.lightRadius", dragonLightRadius_);
  visit("dragon.lightDecay", dragonLightDecay_);
  visit("dragon.weakLight", weakDragonLight_);

  visit("bloom.threshold", bloomThreshold_);
  visit("bloom.intensity", bloomIntensity_);
  visit("bloom.sigma", bloomSigma_);
}
