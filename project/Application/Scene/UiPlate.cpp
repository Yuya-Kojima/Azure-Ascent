#include "UiPlate.h"
#include "Math/MathUtil.h"
#include "Sprite/Sprite.h"

namespace {
const Transform kUvTransform = {
    {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
const float kScreenWidth = 1280.0f; // 画面の幅（px）。板の右端
const float kEdgeWidth = 1.0f;      // 縁の線の太さ（px）
const float kPlateAlpha = 0.9f;     // 板の不透明度
const float kEdgeAlpha = 0.6f;      // 縁の線の不透明度
} // namespace

UiPlate::UiPlate() = default;

UiPlate::~UiPlate() = default;

void UiPlate::Initialize(SpriteRenderer *spriteRenderer) {
  auto create = [spriteRenderer]() {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer, "resources/white1x1.png");
    sprite->SetAnchorPoint({0.0f, 0.0f});
    return sprite;
  };
  plate_ = create();
  for (auto &edge : edges_) {
    edge = create();
  }
}

void UiPlate::Set(float left, float top, float bottom, float alpha) {
  auto place = [](Sprite &sprite, const Vector2 &pos, const Vector2 &size,
                  const float color[3], float a) {
    sprite.SetPosition(pos);
    sprite.SetSize(size);
    sprite.SetColor({color[0], color[1], color[2], a});
    sprite.Update(kUvTransform);
  };
  const float width = kScreenWidth - left;
  const float height = bottom - top;
  const float edgeAlpha = kEdgeAlpha * alpha;
  place(*plate_, {left, top}, {width, height}, UiColor::kPlate,
        kPlateAlpha * alpha);
  place(*edges_[0], {left, top}, {width, kEdgeWidth}, UiColor::kGold,
        edgeAlpha);
  place(*edges_[1], {left, top}, {kEdgeWidth, height}, UiColor::kGold,
        edgeAlpha);
  place(*edges_[2], {left, bottom - kEdgeWidth}, {width, kEdgeWidth},
        UiColor::kGold, edgeAlpha);
}

void UiPlate::Draw() {
  plate_->Draw();
  for (auto &edge : edges_) {
    edge->Draw();
  }
}
