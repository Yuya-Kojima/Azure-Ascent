#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <memory>
#include <vector>

class Sprite;
class SpriteRenderer;

/// <summary>
/// 迎撃しないといけない攻撃の拍を決め、撃てる所（印の元）のまわりの放電と、放たれた電撃を画面に描く
/// 拍は、溜まり切るまでの残りが短くなるほど速くなり、拍ごとに警告音を鳴らす。最後は鳴りっぱなしになる
/// 持ち主（大蛇の光る節など）と自機は、GetBeat・GetThreat を読んで同じ拍で光る
/// 印の元の位置は持ち主が毎フレーム渡す
/// </summary>
class InterceptCue {
public:
  InterceptCue();
  ~InterceptCue();

  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 印を 1 つ足す。返す番号で UpdateMark・BreakMark・RemoveMark を呼ぶ
  /// </summary>
  int AddMark();

  /// <summary>
  /// 印の元の位置と大きさ（半径 m）、溜まり切るまでの残りの秒を渡す
  /// </summary>
  void UpdateMark(int id, const Vector3 &sourceWorld, float sourceRadius, float remainingSeconds);

  /// <summary>
  /// 印の元が撃たれた: 放電が止まり、拍の強さが 1 段下がる
  /// </summary>
  void BreakMark(int id);

  /// <summary>
  /// 印を静かに消す
  /// </summary>
  void RemoveMark(int id);

  /// <summary>
  /// from から to へ電撃を走らせる（見た目だけ。当たりは持ち主が決める）
  /// </summary>
  void PlayZap(const Vector3 &fromWorld, const Vector3 &toWorld);

  /// <summary>
  /// 印と電撃を全部消す
  /// </summary>
  void Clear();

  /// <summary>
  /// 時間を進め、拍を決める。画面の行列も受け取る
  /// </summary>
  void Update(const Matrix4x4 &viewProjection);

  void Draw();

  /// <summary>
  /// 拍の光り具合（拍の瞬間が 1 で、次の拍までに 0 へ下がる。鳴りっぱなしの間は 1。印がなければ 0）
  /// </summary>
  float GetBeat() const { return beat_; }

  /// <summary>
  /// 鳴りっぱなし（溜まり切る直前）か
  /// </summary>
  bool IsSteady() const { return isSteady_; }

  /// <summary>
  /// 狙われている側の光り具合: 拍の光り具合 × 残っている印の数で決まる強さ
  /// </summary>
  float GetThreat() const { return beat_ * strength_; }

private:
  // 印 1 つ
  struct Mark {
    int id = 0;
    Vector3 source = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;      // 印の元の大きさ（半径 m）
    float remaining = 0.0f;   // 溜まり切るまでの残りの秒
    bool isBroken = false;
    bool isUsed = false;      // UpdateMark で位置をもらったか
  };

  // 電撃 1 本
  struct Zap {
    Vector3 from = {0.0f, 0.0f, 0.0f};
    Vector3 to = {0.0f, 0.0f, 0.0f};
    float timer = 0.0f;  // 走り始めてからの秒数
    unsigned int seed = 0; // ぎざぎざの形を決める数
  };

  /// <summary>
  /// 撃たれていない印のうち、残りの秒が最も短いものに合わせて拍を進め、拍ごとに警告音を鳴らす
  /// </summary>
  void UpdateBeat();

  /// <summary>
  /// ワールドの点を画面の座標にする。カメラの後ろなら false
  /// </summary>
  bool ToScreen(const Vector3 &world, Vector2 &screen) const;

  /// <summary>
  /// 今のフレームで次に使うスプライト（足りなければ作る）
  /// </summary>
  Sprite *NextSprite();

  /// <summary>
  /// 白い板を、a から b への太さ thickness の線として描く
  /// </summary>
  void DrawSegment(const Vector2 &a, const Vector2 &b, float thickness, const Vector4 &color);

  /// <summary>
  /// a から b へ、joints 本に区切って間の折れ目を横へ最大 jitter（px）ずらした、ぎざぎざの線を描く
  /// </summary>
  void DrawJagged(const Vector2 &a, const Vector2 &b, int joints, float jitter, unsigned int seed,
                  float thickness, const Vector4 &color);

  /// <summary>
  /// 印の元のまわりに、拍に合わせた放電を描く
  /// </summary>
  void DrawArcs(const Mark &mark);

  void DrawZap(const Zap &zap);

  static constexpr float kFrameTime_ = 1.0f / 60.0f;      // 1 フレームの秒数
  static constexpr float kScreenWidth_ = 1280.0f;         // 画面の幅（px）
  static constexpr float kScreenHeight_ = 720.0f;         // 画面の高さ（px）
  static constexpr float kSlowBeatPeriod_ = 0.5f;         // 残りが kSlowBeatUntil_ 秒より長いときの拍の間（秒）
  static constexpr float kFastBeatPeriod_ = 0.12f;        // 鳴りっぱなしの直前の拍の間（秒）
  static constexpr float kSlowBeatUntil_ = 3.0f;          // 拍が速くなり始める残りの秒
  static constexpr float kSteadyStart_ = 0.3f;            // 鳴りっぱなしにする残りの秒
  static constexpr float kBeatDecayRatio_ = 0.3f;         // 拍の光が 1/e に下がるまでの秒の、拍の間に対する割合
  static constexpr float kWeakestStrength_ = 0.35f;       // 印が 1 つだけ残ったときの強さ（全部残っていれば 1）
  static constexpr int kArcBase_ = 1;                     // 拍の合間にも出ている放電の本数
  static constexpr int kArcOnBeat_ = 3;                   // 拍の瞬間の放電の本数
  static constexpr int kArcSteady_ = 5;                   // 鳴りっぱなしの間の放電の本数
  static constexpr float kArcInner_ = 1.1f;               // 放電の出る所の、中心からの距離（印の元の半径に対する倍率）
  static constexpr float kArcReach_ = 1.0f;               // 拍の合間の放電の先の、出る所からの長さ（同じ倍率）
  static constexpr float kArcReachOnBeat_ = 2.0f;         // 拍の瞬間・鳴りっぱなしの放電の先の長さ（同じ倍率）
  static constexpr int kArcJoints_ = 4;                   // 放電 1 本の折れ目の数
  static constexpr float kArcJitterRatio_ = 0.25f;        // 放電の横のずれの、長さに対する割合
  static constexpr float kArcThickness_ = 3.0f;           // 放電の線の太さ（px）
  static constexpr float kArcBaseAlpha_ = 0.45f;          // 拍の合間の放電の濃さ
  static constexpr float kArcFlickerRate_ = 20.0f;        // 放電の形を変える回数（1 秒あたり）
  static constexpr float kMinSourcePixels_ = 8.0f;        // 放電の大きさを決める、印の元の半径の画面の上の最小（px）
  static constexpr float kZapSeconds_ = 0.45f;            // 電撃が見えている秒数
  static constexpr float kZapTravelSeconds_ = 0.12f;      // 電撃の先が元から行き先まで走る秒数
  static constexpr int kZapJoints_ = 10;                  // 電撃のぎざぎざの折れ目の数
  static constexpr float kZapJitter_ = 18.0f;             // 電撃のぎざぎざの横のずれ（px）
  static constexpr float kZapThickness_ = 5.0f;           // 電撃の線の太さ（px）
  static constexpr float kZapFlickerRate_ = 30.0f;        // 電撃の形を変える回数（1 秒あたり）

  SpriteRenderer *spriteRenderer_ = nullptr;
  Matrix4x4 viewProjection_;
  std::vector<Mark> marks_;
  std::vector<Zap> zaps_;
  float beatPhase_ = 0.0f;   // 今の拍から次の拍までの進み（0〜1）
  float sinceBeat_ = 0.0f;   // 今の拍からの秒数
  float beatPeriod_ = kSlowBeatPeriod_; // 今の拍の間（秒）
  float beat_ = 0.0f;
  float strength_ = 0.0f;
  bool isSteady_ = false;
  float clock_ = 0.0f;       // 放電の形を変える時刻（秒）
  int nextId_ = 1;
  unsigned int nextSeed_ = 1;

  std::vector<std::unique_ptr<Sprite>> sprites_;
  size_t usedSprites_ = 0;
};
