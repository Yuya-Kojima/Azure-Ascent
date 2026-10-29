#pragma once
#include "Scene/DigitText.h"
#include "Scene/UiPlate.h"
#include "Score/ScoreConfig.h"
#include "Score/StageRecord.h"
#include <array>
#include <memory>
#include <string>

class Input;
class Sprite;
class SpriteRenderer;

/// <summary>
/// 出撃前の確認画面（拠点の右の暗い板）
/// 空域名・説明文・ベスト・次のランクの条件・「出撃」「戻る」
/// まだクリアしていない空域は、ベストに「記録なし」と出し、次のランクは出さない
/// </summary>
class StageBriefing {
public:
  /// <summary>
  /// 選んだもの
  /// </summary>
  enum class Choice {
    None,   // まだ選んでいない
    Sortie, // 出撃
    Back,   // 戻る
  };

  StageBriefing();
  ~StageBriefing();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 確認画面を出し始める
  /// </summary>
  /// <param name="stageName">空域のレベルのファイル名（記録を読む）</param>
  /// <param name="config">次のランクの基準</param>
  void Start(const std::string &stageName, const ScoreConfig &config);

  /// <summary>
  /// 更新
  /// </summary>
  /// <param name="input">操作を受けるときの入力。nullptr なら操作を受けない</param>
  void Update(Input *input);

  /// <summary>
  /// 出ているか（選んだあと、消えきると false）
  /// </summary>
  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 選んだもの（一度だけ返し、そのあとは None）
  /// </summary>
  Choice ConsumeChoice();

  void Draw();

private:
  // 次のランクの条件の 1 行（「S まで　撃墜率 あと 5%」）
  struct NeedRow {
    bool isShown = false;
    bool isScore = false; // スコアの条件か（false なら撃墜率）
    std::string text;     // あとどれだけか（「5%」「12,300」）
    DigitText digits;
    std::unique_ptr<Sprite> rank;
    std::unique_ptr<Sprite> until;
  };

  void UpdateSprites();

  bool isActive_ = false;
  bool isLeaving_ = false; // 選んだあと消えていく途中か
  float timer_ = 0.0f;
  float leaveTimer_ = 0.0f;
  Choice pendingChoice_ = Choice::None;
  Choice focused_ = Choice::Sortie;
  float focusPos_ = 0.0f; // 下線の位置（0: 出撃 〜 1: 戻る）

  StageRecord record_;

  UiPlate plate_;
  std::unique_ptr<Sprite> title_;
  std::unique_ptr<Sprite> titleLine_;
  std::array<std::unique_ptr<Sprite>, 2> descLines_;
  std::unique_ptr<Sprite> bestLabel_;
  std::unique_ptr<Sprite> noRecord_;
  std::unique_ptr<Sprite> bestRank_;
  DigitText bestScore_;
  std::unique_ptr<Sprite> bestKillRateLabel_;
  DigitText bestKillRate_;
  std::unique_ptr<Sprite> nextLabel_;
  std::unique_ptr<Sprite> needScoreLabel_;
  std::unique_ptr<Sprite> needRateLabel_;
  std::array<NeedRow, 2> needRows_;
  std::unique_ptr<Sprite> sortie_;
  std::unique_ptr<Sprite> back_;
  std::unique_ptr<Sprite> underline_;
};
