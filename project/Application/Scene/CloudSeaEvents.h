#pragma once
#include "Math/MathUtil.h"
#include <vector>

class CloudSea;

/// <summary>
/// 雲海を演出側から動かす。
/// 時刻つきの出来事（盛り上がり・裂け目）を、今の時刻での形の数値に直して雲海へ渡す。
/// 雲海の見た目の値（起伏・風・濃さ・光など）も持ち、resources/config/CloudSeaConfig.json に保存する
/// </summary>
class CloudSeaEvents {
public:
  enum class Kind {
    Swell, // 雲海の盛り上がり
    Tear,  // 雲海の裂け目
  };

  // 強さの変わり方（2 乗の曲線）
  enum class Ease { Linear, In, Out, InOut };

  /// <summary>
  /// 時刻つきの出来事。
  /// 強さは 開く（riseSec）→ そのまま（holdSec）→ 閉じる（fallSec）で 0 → 1 → 0 と変わる
  /// </summary>
  struct Event {
    Kind kind = Kind::Swell;
    float startTime = 0.0f; // 始まる時刻（秒）
    float riseSec = 0.0f;
    Ease riseEase = Ease::Linear;
    float holdSec = 0.0f;
    float fallSec = 0.0f;
    Ease fallEase = Ease::Linear;
    Vector3 position = {0.0f, 0.0f, 0.0f}; // 中心（XZ を使う）
    float size = 0.0f;   // 盛り上がり: 半径、裂け目: 穴の最大の半径
    float amount = 0.0f; // 盛り上がり: 高さ、裂け目: 縁のめくれの高さ
    float crack = 0.0f;  // 盛り上がり: ひびの濃さ（0〜1）
  };

  // 雲海の見た目の値
  struct Config {
    float height = -15.0f;        // 雲の層の上の面の基準の高さ（ワールドの Y）
    float amplitude = 3.0f;       // 上の面の起伏の高さ（基準から上下へ）
    float blobSize = 30.0f;       // 上の面の塊の大きさ
    Vector2 wind = {0.3f, -1.5f}; // 風で雲が流れる速さ（X, Z。1 秒あたり）
    float thickness = 25.0f;      // 層の厚み（基準の高さから下へ）
    float edgeSoftness = 0.8f;    // 上の面から濃さが 1 になるまでの深さ
    float density = 1.6f;         // 濃さ 1 の所で、長さ 1 あたりに光が弱まる割合
    Vector3 color = {0.16f, 0.16f, 0.17f}; // 雲の色（光を受けて返す割合）
    float brightness = 1.0f;      // 雲が返す光の強さの倍率
    float powder = 1.0f;          // 粉の暗さの強さ（0〜1）
    float valleyDarkness = 0.3f;  // 低い所の環境光を暗くする強さ
    float forwardScatter = 0.0f;  // 光の進む向きへ強く散る度合いの混ぜ具合（0〜1）
    float rimStrength = 0.0f;     // 地平線の光が上の縁を透ける強さ（0 で無効）
    float rimPower = 3.0f;        // 地平線の光の縁の鋭さ
    float noiseSize = 40.0f;      // 大きなノイズ 1 枚のワールドの長さ
    float detailSize = 6.0f;      // 細かいノイズ 1 枚のワールドの長さ
    float detailStrength = 0.35f; // 細かいノイズで縁を削る強さ
    float jitter = 1.0f;          // 視線の出発点を画素ごとにずらす量（0〜1）
  };

  /// <summary>
  /// 設定ファイルを読み込む（ないときは既定値のまま）
  /// </summary>
  void Initialize();

  /// <summary>
  /// 出来事を足す
  /// </summary>
  void Add(const Event &event);
  /// <summary>
  /// 出来事を全部消す
  /// </summary>
  void Clear();

  /// <summary>
  /// 時刻を決める（終わった出来事は消す）
  /// </summary>
  void SetTime(float time);
  /// <summary>
  /// 時刻を進める（終わった出来事は消す）
  /// </summary>
  void Advance(float deltaTime) { SetTime(time_ + deltaTime); }
  float GetTime() const { return time_; }

  /// <summary>
  /// 雲海の見た目の値と、今の時刻での形を雲海へ渡す
  /// </summary>
  void Apply(CloudSea *cloudSea) const;

  /// <summary>
  /// 雲海の見た目の調整用 ImGui（呼び出し側の窓の中に描く）
  /// </summary>
  void DrawImGui();

private:
  /// <summary>
  /// 出来事の強さ（0〜1。始まる前は負の値）
  /// </summary>
  static float CalcStrength(const Event &event, float time);
  static float ApplyEase(Ease ease, float t);

  void LoadConfig();
  void SaveConfig() const;

  Config config_;
  // 雲海の調べるための表示（CloudSea::DebugView の番号。保存しない）
  int debugView_ = 0;
  std::vector<Event> events_;
  float time_ = 0.0f;
};
