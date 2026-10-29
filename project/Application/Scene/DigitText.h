#pragma once
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include <memory>
#include <string>
#include <vector>

class Sprite;
class SpriteRenderer;

/// <summary>
/// 数字画像（0〜9・カンマ・%・/ を横 1 列に並べた 1 枚の画像）から 1 文字ずつ切り出して並べ、数を出す
/// 画像と文字ごとの位置は tools/IntroText/make_intro_text.py の ATLASES で書き出す
/// </summary>
class DigitText {
public:
  /// <summary>
  /// 数字の色
  /// </summary>
  enum class Style {
    Gold, // 金（暗い影）
    Glow, // 金の光
    Pale, // 淡い白（暗い影）
  };

  DigitText();
  ~DigitText();

  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="maxChars">一度に出せる文字数</param>
  void Initialize(SpriteRenderer *spriteRenderer, Style style,
                  size_t maxChars);

  /// <summary>
  /// 文字を置く
  /// </summary>
  /// <param name="text">数字・カンマ・%・/ と空白（空白は数字の半分の幅を空ける）</param>
  /// <param name="pos">横は alignX で決める基準の位置、縦は文字の縦の中心（px）</param>
  /// <param name="size">文字の大きさ（px）</param>
  /// <param name="alignX">0: pos が左端、0.5: 中央、1: 右端（縁の余白を除いた文字の幅で）</param>
  void Set(const std::string &text, const Vector2 &pos, float size,
           float alignX, const Vector4 &color);

  /// <summary>
  /// 文字の幅（縁の余白を除く、px）
  /// </summary>
  float GetWidth(const std::string &text, float size) const;

  /// <summary>
  /// 描画（Set で置いた文字だけ）
  /// </summary>
  void Draw();

  /// <summary>
  /// 3 桁ごとにカンマを入れた数の文字
  /// </summary>
  static std::string FormatNumber(int value);

private:
  struct Glyph {
    float x = 0.0f;       // 画像の中の左端（px）
    float width = 0.0f;   // 縁の余白を含む幅（px）
    float advance = 0.0f; // 次の文字までの送り幅（px）
  };

  // 文字の位置（なければ nullptr）
  const Glyph *FindGlyph(char ch) const;

  std::string texturePath_;
  float baseSize_ = 64.0f;   // 画像に描いた文字の大きさ（px）
  float pad_ = 0.0f;         // 縁の余白（px）
  float height_ = 0.0f;      // 画像の高さ（px）
  Glyph glyphs_[128]{};
  bool hasGlyph_[128]{};
  std::vector<std::unique_ptr<Sprite>> sprites_;
  size_t usedCount_ = 0; // Set で置いた文字の数
};
