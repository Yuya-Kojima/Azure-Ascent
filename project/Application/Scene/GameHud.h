#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Scene/DigitText.h"
#include <array>
#include <memory>
#include <string>

class Boss;
class Player;
class ScoreKeeper;
class Sprite;
class SpriteRenderer;

/// <summary>
/// プレイ中の HUD（左下に HP、右下にスコア、照準の下にロック数、倒した敵の近くに得点、ボス戦の間は上の真ん中にボスの体力）
/// </summary>
class GameHud {
public:
  GameHud();
  ~GameHud();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// プレイの始めに表示を戻す
  /// </summary>
  void Reset();

  /// <summary>
  /// 更新（1 フレームに 1 回）
  /// </summary>
  /// <param name="boss">出ているボス（いなければ nullptr）</param>
  /// <param name="viewProjection">得点の文字を倒した敵の近くに出すためのカメラの行列</param>
  /// <param name="alpha">HUD 全体の不透明度（開始演出のあとに現す）</param>
  void Update(const Player &player, ScoreKeeper &scoreKeeper,
              const Boss *boss, const Matrix4x4 &viewProjection, float alpha);

  void Draw();

private:
  // 倒した敵の近くに出す得点の文字
  struct Popup {
    bool isActive = false;
    Vector3 worldPosition{};
    std::string text;
    float size = 0.0f; // 文字の大きさ（px）
    float timer = 0.0f;
    std::unique_ptr<DigitText> digits;
  };

  // 最大ロックのカード（線の上に言葉、下に得点）
  struct MaxLockCard {
    bool isActive = false;
    Vector3 worldPosition{};
    std::string text;
    float timer = 0.0f;
  };

  void UpdateHpBar(const Player &player);
  void UpdateScore(int score);
  void UpdateLockMarks(const Player &player);
  void UpdatePopups(ScoreKeeper &scoreKeeper, const Matrix4x4 &viewProjection);
  void UpdateBossBar(const Boss *boss);

  // 暗い下地・金の枠・中身の、横に伸びる体力のバー
  struct Bar {
    std::unique_ptr<Sprite> base;
    std::array<std::unique_ptr<Sprite>, 4> frame; // 上・下・左・右
    std::unique_ptr<Sprite> fill;
  };
  void InitializeBar(Bar &bar, SpriteRenderer *spriteRenderer);
  // 左上 (left, top) から幅 width・高さ height のバーを、中身の割合 ratio で置く
  void PlaceBar(Bar &bar, float left, float top, float width, float height,
                float ratio, const float fillColor[3], float alpha);
  void DrawBar(Bar &bar);

  float alpha_ = 1.0f;

  // HP
  Bar hpBar_;
  float shownHpRatio_ = 1.0f; // 表示している HP の割合（減ると追いかける）

  // スコア
  std::unique_ptr<Sprite> scoreLabel_;
  DigitText scoreDigits_;
  float shownScore_ = 0.0f; // 数え上げている途中のスコア

  // ロック数
  static constexpr int kLockMarkCount = 8;
  std::array<std::unique_ptr<Sprite>, kLockMarkCount> lockOnMarks_;
  std::array<std::unique_ptr<Sprite>, kLockMarkCount> lockOffMarks_;
  std::array<float, kLockMarkCount> lockPopTimers_{}; // ロックした瞬間からの時間（秒）
  int lockCount_ = 0;
  float lockAlpha_ = 0.0f; // ロック中に現れ、撃つと消える
  bool isLockOnMode_ = false;

  // 得点
  static constexpr int kPopupCount = 8;
  std::array<Popup, kPopupCount> popups_;
  MaxLockCard maxLockCard_;
  std::unique_ptr<Sprite> maxLockWord_;
  std::unique_ptr<Sprite> maxLockLine_;
  DigitText maxLockDigits_;

  // ボスの体力
  Bar bossBar_;
  std::unique_ptr<Sprite> bossLabel_;
  bool isBossShown_ = false;
  float bossAlpha_ = 0.0f;
  float shownBossRatio_ = 1.0f;
};
