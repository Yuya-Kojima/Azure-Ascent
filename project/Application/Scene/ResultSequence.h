#pragma once
#include "Scene/DigitText.h"
#include "Scene/UiPlate.h"
#include "Score/ScoreConfig.h"
#include "Score/ScoreKeeper.h"
#include <array>
#include <memory>

class Input;
class Sprite;
class SpriteRenderer;

/// <summary>
/// クリアしたあとのリザルト（右の暗い板）
/// 板と空域名 → 内訳を上から 1 行ずつ数え上げ → スコアと文字 → 撃墜率と文字 → 総合ランク
/// → ベスト更新 → ヒント → 選択肢「もう一度飛ぶ」「拠点へ戻る」の順に出す
/// </summary>
class ResultSequence {
public:
  /// <summary>
  /// 選んだもの
  /// </summary>
  enum class Choice {
    None,  // まだ選んでいない
    Again, // もう一度飛ぶ
    Base,  // 拠点へ戻る
  };

  /// <summary>
  /// 出す結果
  /// </summary>
  struct Result {
    ScoreKeeper::Breakdown breakdown;
    int spawned = 0; // 出てきた敵の数
    Rank scoreRank = Rank::C;
    Rank killRateRank = Rank::C;
    Rank overallRank = Rank::C;
    int hintSection = -1;         // いちばん多く逃した区間（-1 なら全機撃墜）
    bool isNewBest = false;       // ベストスコアを更新したか
    bool hasPreviousBest = false; // 前のベストがあったか
    int previousBestScore = 0;
    int bestScore = 0;            // 今のベストスコア
  };

  ResultSequence();
  ~ResultSequence();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// リザルトを出し始める
  /// </summary>
  void Start(const Result &result);

  /// <summary>
  /// 更新
  /// </summary>
  /// <param name="input">操作を受けるときの入力。nullptr なら操作を受けない</param>
  void Update(Input *input);

  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 選んだもの（一度だけ返し、そのあとは None）
  /// </summary>
  Choice ConsumeChoice();

  void Draw();

private:
  // 内訳の 1 行（撃破・まとめ撃ち・最大ロック・ボス撃破）
  struct Row {
    std::unique_ptr<Sprite> label;
    std::unique_ptr<Sprite> unit; // 「体」「回」（なければ nullptr）
    DigitText count;              // 数の補足（倒した数・回数）
    DigitText points;
    int countValue = -1; // 数の補足（-1 なら出さない）
    int pointsValue = 0;
  };

  void UpdateMenu(Input *input);
  void UpdateSprites();

  // 選択肢が現れきって選べるようになるまでの時間（秒）
  float GetReadySec() const;

  bool isActive_ = false;
  float timer_ = 0.0f;
  Result result_;
  Choice pendingChoice_ = Choice::None;
  bool hasChosen_ = false; // 選び終わったか（そのあとは操作を受けない）
  Choice focused_ = Choice::Again;
  float focusPos_ = 0.0f; // 下線の位置（0: もう一度飛ぶ 〜 1: 拠点へ戻る）

  UiPlate plate_;
  std::unique_ptr<Sprite> title_;
  std::unique_ptr<Sprite> reclaim_;
  std::unique_ptr<Sprite> titleLine_;
  static constexpr int kRowCount = 4;
  std::array<Row, kRowCount> rows_;
  std::unique_ptr<Sprite> divider_;
  std::unique_ptr<Sprite> scoreLabel_;
  DigitText scoreDigits_;
  std::unique_ptr<Sprite> scoreRank_;
  std::unique_ptr<Sprite> killRateLabel_;
  DigitText killCountDigits_; // 「36 / 40」
  DigitText killRateDigits_;  // 「90%」
  std::unique_ptr<Sprite> killRateRank_;
  std::unique_ptr<Sprite> totalLabel_;
  std::unique_ptr<Sprite> totalRank_;
  std::unique_ptr<Sprite> bestUpdate_;
  std::unique_ptr<Sprite> bestLabel_; // 更新したら「前回」、しなければ「ベスト」
  std::unique_ptr<Sprite> prevLabel_;
  DigitText bestDigits_;
  std::unique_ptr<Sprite> hintLabel_;
  std::array<std::unique_ptr<Sprite>, ScoreKeeper::kSectionCount> hintSections_; // 序盤・中盤・終盤
  std::unique_ptr<Sprite> hintPerfect_;
  std::unique_ptr<Sprite> again_;
  std::unique_ptr<Sprite> base_;
  std::unique_ptr<Sprite> underline_;
};
