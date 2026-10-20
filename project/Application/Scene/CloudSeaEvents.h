#pragma once
#include "Math/MathUtil.h"
#include <vector>

class CloudSea;

/// <summary>
/// 雲海を演出側から動かす。
/// 時刻つきの出来事（盛り上がり・裂け目）を、今の時刻での形の数値に直して雲海へ渡す。
/// 雲海の見た目の値（起伏・風・谷の暗さ・色など）も持ち、resources/config/CloudSeaConfig.json に保存する
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
    float height = -15.0f;                        // 雲海の面の高さ（ワールドの Y）
    float amplitude = 3.0f;                       // 起伏の高さ（面から上下へ）
    float blobSize = 30.0f;                       // 雲の塊の大きさ
    Vector2 wind = {0.3f, -1.5f};                 // 風で模様が流れる速さ（X, Z。1 秒あたり）
    float valleyDarkness = 0.6f;                  // 谷を暗くする強さ
    float rimStrength = 0.0f;                     // 逆光の縁の強さ（0 で無効）
    float rimPower = 3.0f;                        // 逆光の縁の鋭さ
    Vector3 color = {0.16f, 0.16f, 0.17f};        // 雲海の色
    float textureScale = 0.01f;                   // 模様の繰り返しの細かさ（長さ 1 あたり）
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
  std::vector<Event> events_;
  float time_ = 0.0f;
};
