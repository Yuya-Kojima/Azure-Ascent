#include "DigitText.h"
#include "../../externals/nlohmann/json.hpp"
#include "Math/MathUtil.h"
#include "Sprite/Sprite.h"
#include <fstream>

namespace {
// UV は切り出した範囲をそのまま使う
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
// 空白の幅（数字 1 文字の送り幅に対する割合）
const float kSpaceAdvanceRate = 0.5f;

/// <summary>
/// 数字の色ごとの画像の名前（resources/UI/Score/ の .png と .json）
/// </summary>
const char *StyleName(DigitText::Style style) {
  switch (style) {
  case DigitText::Style::Glow:
    return "digits_glow";
  case DigitText::Style::Pale:
    return "digits_pale";
  case DigitText::Style::Gold:
  default:
    return "digits_gold";
  }
}
} // namespace

DigitText::DigitText() = default;

DigitText::~DigitText() = default;

void DigitText::Initialize(SpriteRenderer *spriteRenderer, Style style,
                           size_t maxChars) {
  const std::string base = std::string("resources/UI/Score/") + StyleName(style);
  texturePath_ = base + ".png";

  // 文字ごとの位置を読む
  std::ifstream file(base + ".json");
  if (file.is_open()) {
    nlohmann::json root;
    try {
      file >> root;
      baseSize_ = root.value("size", 64.0f);
      pad_ = root.value("pad", 0.0f);
      height_ = root.value("height", 0.0f);
      for (auto &[key, value] : root["glyphs"].items()) {
        if (key.size() != 1) {
          continue;
        }
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (ch >= 128) {
          continue;
        }
        glyphs_[ch] = {value.value("x", 0.0f), value.value("width", 0.0f),
                       value.value("advance", 0.0f)};
        hasGlyph_[ch] = true;
      }
    } catch (const nlohmann::json::exception &) {
    }
  }

  sprites_.clear();
  for (size_t i = 0; i < maxChars; ++i) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, texturePath_);
    sprite->SetAnchorPoint({0.0f, 0.5f});
    sprites_.push_back(std::move(sprite));
  }
  usedCount_ = 0;
}

const DigitText::Glyph *DigitText::FindGlyph(char ch) const {
  const unsigned char index = static_cast<unsigned char>(ch);
  if (index >= 128 || !hasGlyph_[index]) {
    return nullptr;
  }
  return &glyphs_[index];
}

float DigitText::GetWidth(const std::string &text, float size) const {
  const float scale = size / baseSize_;
  const Glyph *zero = FindGlyph('0');
  float width = 0.0f;
  for (char ch : text) {
    if (const Glyph *glyph = FindGlyph(ch)) {
      width += glyph->advance;
    } else if (ch == ' ' && zero) {
      width += zero->advance * kSpaceAdvanceRate;
    }
  }
  return width * scale;
}

void DigitText::Set(const std::string &text, const Vector2 &pos, float size,
                    float alignX, const Vector4 &color) {
  const float scale = size / baseSize_;
  const Glyph *zero = FindGlyph('0');
  float penX = pos.x - GetWidth(text, size) * alignX;
  usedCount_ = 0;
  for (char ch : text) {
    const Glyph *glyph = FindGlyph(ch);
    if (!glyph) {
      if (ch == ' ' && zero) {
        penX += zero->advance * kSpaceAdvanceRate * scale;
      }
      continue;
    }
    if (usedCount_ >= sprites_.size()) {
      break;
    }
    // 文字の形（縁の余白を除いた幅）を送り幅の真ん中に置く
    const float inkWidth = glyph->width - pad_ * 2.0f;
    const float left = penX + ((glyph->advance - inkWidth) * 0.5f - pad_) * scale;
    Sprite &sprite = *sprites_[usedCount_++];
    sprite.SetTextureLeftTop({glyph->x, 0.0f});
    sprite.SetTextureSize({glyph->width, height_});
    sprite.SetSize({glyph->width * scale, height_ * scale});
    sprite.SetPosition({left, pos.y});
    sprite.SetColor(color);
    sprite.Update(kUvTransform);
    penX += glyph->advance * scale;
  }
}

void DigitText::Draw() {
  for (size_t i = 0; i < usedCount_; ++i) {
    sprites_[i]->Draw();
  }
}

std::string DigitText::FormatNumber(int value) {
  const bool isNegative = value < 0;
  std::string digits = std::to_string(isNegative ? -value : value);
  std::string result;
  const int length = static_cast<int>(digits.size());
  for (int i = 0; i < length; ++i) {
    result += digits[i];
    const int remain = length - 1 - i;
    if (remain > 0 && remain % 3 == 0) {
      result += ',';
    }
  }
  return isNegative ? "-" + result : result;
}
