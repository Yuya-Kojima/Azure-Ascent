#include "Actor/InterceptCue.h"
#include "Audio/SoundManager.h"
#include "Math/MathUtil.h"
#include "Math/Transform.h"
#include "Render/Sprite/Sprite.h"
#include <algorithm>
#include <cmath>

namespace {
const char *const kWhiteTexturePath = "resources/white1x1.png"; // 線を描く白い板
constexpr float kTwoPi = 6.2831853f;                             // 1 周の角度（ラジアン）
const char *const kBeatSoundKey = "intercept_warn";              // 拍ごとに鳴らす警告音
const char *const kSteadySoundKey = "intercept_lock";            // 鳴りっぱなしになったときに 1 回鳴らす長い警告音
constexpr Vector4 kZapColor = {1.6f, 1.3f, 2.2f, 1.0f};          // 電撃と放電の色（紫がかった白）
constexpr Vector3 kWorldUp = {0.0f, 1.0f, 0.0f};
constexpr float kArcBend = 0.6f;                                 // 放電の先を、出る所の向きから振る最大の角度（ラジアン）
// UV は画像全体をそのまま使う
const Transform kUvTransform = {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};

// 0〜1 の x を、両端の傾きが 0 になるようにならす
float SmoothStep01(float x) {
  x = std::clamp(x, 0.0f, 1.0f);
  return x * x * (3.0f - 2.0f * x);
}

// seed と番号から -1〜1 の数を作る
float HashSigned(unsigned int seed, int index) {
  unsigned int h = seed * 747796405u + static_cast<unsigned int>(index) * 2891336453u;
  h ^= h >> 16;
  h *= 2246822519u;
  h ^= h >> 13;
  return static_cast<float>(h & 0xffffu) / 32767.5f - 1.0f;
}
} // namespace

InterceptCue::InterceptCue() { viewProjection_ = MakeIdentity4x4(); }
InterceptCue::~InterceptCue() = default;

void InterceptCue::Initialize(SpriteRenderer *spriteRenderer) {
  spriteRenderer_ = spriteRenderer;
  Clear();
}

int InterceptCue::AddMark() {
  Mark mark;
  mark.id = nextId_++;
  marks_.push_back(mark);
  return mark.id;
}

void InterceptCue::UpdateMark(int id, const Vector3 &sourceWorld, float sourceRadius, float remainingSeconds) {
  for (Mark &mark : marks_) {
    if (mark.id == id && !mark.isBroken) {
      mark.source = sourceWorld;
      mark.radius = (std::max)(sourceRadius, 0.0f);
      mark.remaining = (std::max)(remainingSeconds, 0.0f);
      mark.isUsed = true;
      return;
    }
  }
}

void InterceptCue::BreakMark(int id) {
  for (Mark &mark : marks_) {
    if (mark.id == id) {
      mark.isBroken = true;
      return;
    }
  }
}

void InterceptCue::RemoveMark(int id) {
  marks_.erase(std::remove_if(marks_.begin(), marks_.end(),
                              [id](const Mark &mark) { return mark.id == id; }),
               marks_.end());
}

void InterceptCue::PlayZap(const Vector3 &fromWorld, const Vector3 &toWorld) {
  Zap zap;
  zap.from = fromWorld;
  zap.to = toWorld;
  zap.seed = nextSeed_++;
  zaps_.push_back(zap);
}

void InterceptCue::Clear() {
  marks_.clear();
  zaps_.clear();
  beatPhase_ = 1.0f;
  sinceBeat_ = 0.0f;
  beatPeriod_ = kSlowBeatPeriod_;
  beat_ = 0.0f;
  strength_ = 0.0f;
  isSteady_ = false;
}

void InterceptCue::Update(const Matrix4x4 &viewProjection) {
  viewProjection_ = viewProjection;
  clock_ += kFrameTime_;

  for (Zap &zap : zaps_) {
    zap.timer += kFrameTime_;
  }
  zaps_.erase(std::remove_if(zaps_.begin(), zaps_.end(),
                             [](const Zap &zap) { return zap.timer >= kZapSeconds_; }),
              zaps_.end());

  UpdateBeat();
}

void InterceptCue::UpdateBeat() {
  // 撃たれていない印の数と、そのうち残りの秒が最も短いもの
  int live = 0;
  float remaining = -1.0f;
  for (const Mark &mark : marks_) {
    if (mark.isBroken) {
      continue;
    }
    live++;
    if (mark.isUsed && (remaining < 0.0f || mark.remaining < remaining)) {
      remaining = mark.remaining;
    }
  }
  if (live == 0) {
    // 全部撃たれたら、撃たれた印も外して、次の攻撃は拍の頭から始める
    marks_.clear();
    beatPhase_ = 1.0f;
    sinceBeat_ = 0.0f;
    beat_ = 0.0f;
    strength_ = 0.0f;
    isSteady_ = false;
    return;
  }

  // 強さ: 全部残っていれば 1。1 つ撃つごとに同じ幅で下がり、最後の 1 つで kWeakestStrength_
  const int total = static_cast<int>(marks_.size());
  strength_ = total <= 1 ? 1.0f
                         : kWeakestStrength_ + (1.0f - kWeakestStrength_) * static_cast<float>(live - 1) /
                                                   static_cast<float>(total - 1);

  // 残りが kSteadyStart_ 秒を切ったら鳴りっぱなし。切った瞬間に長い警告音を 1 回鳴らす
  if (remaining >= 0.0f && remaining <= kSteadyStart_) {
    if (!isSteady_) {
      SoundManager::GetInstance()->PlaySE(kSteadySoundKey);
    }
    isSteady_ = true;
    beat_ = 1.0f;
    return;
  }
  isSteady_ = false;

  // 拍の間: 残り kSlowBeatUntil_ 秒までは遅いまま、そこから鳴りっぱなしの直前まで縮める
  const float k = remaining < 0.0f ? 1.0f
                                   : std::clamp((remaining - kSteadyStart_) / (kSlowBeatUntil_ - kSteadyStart_),
                                                0.0f, 1.0f);
  beatPeriod_ = kFastBeatPeriod_ + (kSlowBeatPeriod_ - kFastBeatPeriod_) * k;

  // 拍の進みが 1 に届いたら拍を打ち、警告音を鳴らす
  sinceBeat_ += kFrameTime_;
  beatPhase_ += kFrameTime_ / beatPeriod_;
  if (beatPhase_ >= 1.0f) {
    beatPhase_ = std::fmod(beatPhase_, 1.0f);
    sinceBeat_ = 0.0f;
    SoundManager::GetInstance()->PlaySE(kBeatSoundKey);
  }
  // 拍の光: 拍の瞬間に 1、拍の間に比例した速さで下がる
  beat_ = std::exp(-sinceBeat_ / (beatPeriod_ * kBeatDecayRatio_));
}

bool InterceptCue::ToScreen(const Vector3 &world, Vector2 &screen) const {
  // 行ベクトル × 行列で、クリップ座標の x・y・w を出す
  const Matrix4x4 &m = viewProjection_;
  const float x = world.x * m.m[0][0] + world.y * m.m[1][0] + world.z * m.m[2][0] + m.m[3][0];
  const float y = world.x * m.m[0][1] + world.y * m.m[1][1] + world.z * m.m[2][1] + m.m[3][1];
  const float w = world.x * m.m[0][3] + world.y * m.m[1][3] + world.z * m.m[2][3] + m.m[3][3];
  if (w <= 1e-3f) {
    return false;
  }
  screen.x = (x / w + 1.0f) * 0.5f * kScreenWidth_;
  screen.y = (1.0f - y / w) * 0.5f * kScreenHeight_;
  return true;
}

Sprite *InterceptCue::NextSprite() {
  if (usedSprites_ >= sprites_.size()) {
    auto sprite = std::make_unique<Sprite>();
    sprite->Initialize(spriteRenderer_, kWhiteTexturePath);
    sprites_.push_back(std::move(sprite));
  }
  return sprites_[usedSprites_++].get();
}

void InterceptCue::DrawSegment(const Vector2 &a, const Vector2 &b, float thickness, const Vector4 &color) {
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.5f || color.w <= 0.0f) {
    return;
  }
  // 左の真ん中を a に合わせ、b の方へ回した細長い板
  Sprite *sprite = NextSprite();
  sprite->SetAnchorPoint({0.0f, 0.5f});
  sprite->SetPosition(a);
  sprite->SetSize({length, thickness});
  sprite->SetRotation(std::atan2(dy, dx));
  sprite->SetColor(color);
  sprite->Update(kUvTransform);
  sprite->Draw();
}

void InterceptCue::DrawJagged(const Vector2 &a, const Vector2 &b, int joints, float jitter, unsigned int seed,
                              float thickness, const Vector4 &color) {
  const Vector2 d = {b.x - a.x, b.y - a.y};
  const float length = std::sqrt(d.x * d.x + d.y * d.y);
  if (length < 1.0f || joints < 1) {
    return;
  }
  // a から b への直線を joints 個に区切り、間の折れ目だけを横へずらす
  const Vector2 side = {-d.y / length, d.x / length};
  Vector2 prev = a;
  for (int i = 1; i <= joints; ++i) {
    const float k = static_cast<float>(i) / static_cast<float>(joints);
    const float offset = (i == joints) ? 0.0f : HashSigned(seed, i) * jitter;
    const Vector2 p = {a.x + d.x * k + side.x * offset, a.y + d.y * k + side.y * offset};
    DrawSegment(prev, p, thickness, color);
    prev = p;
  }
}

void InterceptCue::Draw() {
  usedSprites_ = 0;
  if (!spriteRenderer_) {
    return;
  }
  for (const Mark &mark : marks_) {
    if (!mark.isBroken && mark.isUsed) {
      DrawArcs(mark);
    }
  }
  for (const Zap &zap : zaps_) {
    DrawZap(zap);
  }
}

void InterceptCue::DrawArcs(const Mark &mark) {
  Vector2 center;
  Vector2 edge;
  if (!ToScreen(mark.source, center) || !ToScreen(mark.source + kWorldUp * mark.radius, edge)) {
    return;
  }
  // 印の元の画面の上の半径から、放電の大きさを決める
  const float radius = (std::max)(std::sqrt((edge.x - center.x) * (edge.x - center.x) +
                                            (edge.y - center.y) * (edge.y - center.y)),
                                  kMinSourcePixels_);

  // 本数・長さ・濃さ: 拍の瞬間に増えて伸び、拍の合間は 1 本だけ薄く残る。鳴りっぱなしの間はいちばん強いまま
  const float strength = isSteady_ ? 1.0f : beat_;
  const int count = isSteady_ ? kArcSteady_
                              : kArcBase_ + static_cast<int>(std::lround(static_cast<float>(kArcOnBeat_ - kArcBase_) * beat_));
  const float reach = kArcReach_ + (kArcReachOnBeat_ - kArcReach_) * strength;
  const Vector4 color = {kZapColor.x, kZapColor.y, kZapColor.z, kArcBaseAlpha_ + (1.0f - kArcBaseAlpha_) * strength};

  // 形は一定の間隔で変える。印ごとに違う形にする
  const unsigned int frame = static_cast<unsigned int>(clock_ * kArcFlickerRate_);
  for (int j = 0; j < count; ++j) {
    const unsigned int seed = static_cast<unsigned int>(mark.id) * 977u + frame * 31u + static_cast<unsigned int>(j);
    const float angle = (HashSigned(seed, 0) * 0.5f + 0.5f) * kTwoPi;
    const float endAngle = angle + HashSigned(seed, 1) * kArcBend;
    const Vector2 start = {center.x + std::cos(angle) * radius * kArcInner_,
                           center.y + std::sin(angle) * radius * kArcInner_};
    const float endRadius = radius * (kArcInner_ + reach);
    const Vector2 end = {center.x + std::cos(endAngle) * endRadius, center.y + std::sin(endAngle) * endRadius};
    const float length = radius * reach;
    DrawJagged(start, end, kArcJoints_, length * kArcJitterRatio_, seed, kArcThickness_, color);
  }
}

void InterceptCue::DrawZap(const Zap &zap) {
  Vector2 from;
  Vector2 to;
  if (!ToScreen(zap.from, from) || !ToScreen(zap.to, to)) {
    return;
  }
  // 先が元から行き先へ走り、走り切ったあとは全体が薄くなる
  const float reach = std::clamp(zap.timer / kZapTravelSeconds_, 0.0f, 1.0f);
  const float fade = 1.0f - SmoothStep01((zap.timer - kZapTravelSeconds_) / (kZapSeconds_ - kZapTravelSeconds_));
  const Vector2 tip = {from.x + (to.x - from.x) * reach, from.y + (to.y - from.y) * reach};
  // ぎざぎざの折れ目は、一定の間隔で形を変える。走っている間は、先までの分だけ描く
  const unsigned int frameSeed = zap.seed * 131u + static_cast<unsigned int>(zap.timer * kZapFlickerRate_);
  const int joints = (std::max)(static_cast<int>(std::ceil(static_cast<float>(kZapJoints_) * reach)), 1);
  DrawJagged(from, tip, joints, kZapJitter_, frameSeed, kZapThickness_, {kZapColor.x, kZapColor.y, kZapColor.z, fade});
}
