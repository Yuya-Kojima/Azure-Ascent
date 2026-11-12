#pragma once
#include "Actor/MechSerpent.h"
#include <functional>
#include <string>

class RailCamera;

/// <summary>
/// 区間 3 の大蛇の出来事に合わせて、カメラとレールの速さを動かす
/// 溜め始めたら画角を狭めて保って戻す合図を出し、最後の見せ場の溜めが始まったらレールを遅くし、最後の見せ場が終わったら速さと顔の向きを戻す
/// 最後の見せ場の大蛇が出てから終わるまでは、顔の向きのキーを決めた t の所で止める
/// </summary>
class SerpentCameraDirector {
public:
  /// <summary>
  /// 調整値をファイルから読む
  /// </summary>
  void Initialize();

  /// <summary>
  /// 大蛇の出来事 1 つを受けて、カメラとレールを動かす
  /// </summary>
  void OnEvent(MechSerpent::Event event, const MechSerpent &serpent, RailCamera &railCamera);

  /// <summary>
  /// エディタで調整値を直す・保存する
  /// </summary>
  void DrawImGui();

  void LoadConfig();
  void SaveConfig();

private:
  /// <summary>
  /// 調整値を名前と一緒に 1 つずつ渡す（読み書きと ImGui で同じ並びを使う）
  /// </summary>
  void VisitConfigValues(const std::function<void(const std::string &key, float &value)> &visit);

  // 光り始めの合図
  float punchFovScale_ = 0.85f;        // 押し込んだときの画角の倍率（小さいほど大きく映る）
  float previewPunchFovScale_ = 0.92f; // 予告（撃てない出番）のときの画角の倍率
  float punchInSeconds_ = 0.25f;       // 押し込む秒数
  float punchHoldSeconds_ = 0.5f;      // 保つ秒数
  float punchOutSeconds_ = 0.6f;       // 戻す秒数

  // 最後の見せ場の間のレールの速さ
  float climaxSpeedScale_ = 0.1f;      // 最後の見せ場の溜めの間にレールの速さに掛ける倍率
  float climaxSlowSeconds_ = 1.0f;     // 遅くするまでの秒数
  float climaxRestoreSeconds_ = 1.0f;  // 最後の見せ場が終わってから速さを戻す秒数

  // 最後の見せ場の間の顔の向き
  float climaxLookHoldT_ = 17.8f;      // 最後の見せ場の大蛇が出てから終わるまで、顔の向きのキーを止める t
  float climaxLookReleaseSeconds_ = 2.0f; // 最後の見せ場が終わってから顔の向きを今の t の値へ戻す秒数
};
