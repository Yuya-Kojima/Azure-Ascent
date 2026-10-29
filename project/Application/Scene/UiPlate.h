#pragma once
#include <array>
#include <memory>

class Sprite;
class SpriteRenderer;

/// <summary>
/// UI の後ろに敷く、画面の右端までの暗い板（上・左・下の縁に金の細い線）
/// </summary>
class UiPlate {
public:
  UiPlate();
  ~UiPlate();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 板を置く（右端は画面の右端）
  /// </summary>
  /// <param name="left">左端（px）</param>
  /// <param name="top">上端（px）</param>
  /// <param name="bottom">下端（px）</param>
  /// <param name="alpha">全体の不透明度（0〜1）</param>
  void Set(float left, float top, float bottom, float alpha);

  void Draw();

private:
  std::unique_ptr<Sprite> plate_;
  std::array<std::unique_ptr<Sprite>, 3> edges_; // 上・左・下の縁の線
};

/// <summary>
/// 評価の UI でそろえる色
/// </summary>
namespace UiColor {
// 金（主な文字・数字・板の縁の線）
constexpr float kGold[3] = {0.91f, 0.78f, 0.51f};
// 竜の水色（自分の HP）
constexpr float kCyan[3] = {0.5f, 0.88f, 0.94f};
// 敵の赤（ボス）
constexpr float kBossRed[3] = {1.0f, 0.2f, 0.27f};
// 板の暗い色（空の暗い色）
constexpr float kPlate[3] = {8.0f / 255.0f, 10.0f / 255.0f, 16.0f / 255.0f};
} // namespace UiColor
