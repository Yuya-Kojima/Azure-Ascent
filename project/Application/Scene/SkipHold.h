#pragma once
#include "Math/MathUtil.h"
#include <memory>

class Input;
class Sprite;
class SpriteRenderer;

/// <summary>
/// 演出を飛ばす長押し（決定を押し続けて、右下の線が伸びきったら飛ばす）
/// 押している間だけ「SPACE 長押しでスキップ」と線を出し、離すと線が縮んで文字は少し残ってから消える
/// 始める前から押しっぱなしの入力は、いったん離すまで数えない
/// </summary>
class SkipHold {
public:
  SkipHold();
  ~SkipHold();

  /// <summary>
  /// 初期化（文字の画像と線のスプライトを作る）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 始める（表示を消し、今押している入力はいったん離すまで数えない）
  /// </summary>
  void Start();

  /// <summary>
  /// 更新（押している時間を数え、表示を決める）
  /// </summary>
  /// <param name="canSkip">今飛ばせるか。false の間は数えず、表示も消していく</param>
  /// <returns>押し続けて線が伸びきったフレームだけ true</returns>
  bool Update(Input *input, bool canSkip);

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

  /// <summary>
  /// 調整用UI
  /// </summary>
  void DrawImGui();

private:
  // 今の押している時間と不透明度で、各スプライトの位置・大きさ・色を決める
  void UpdateSprites();

  std::unique_ptr<Sprite> text_;  // 「SPACE 長押しでスキップ」
  std::unique_ptr<Sprite> track_; // 線の下地（伸びきったときの長さ）
  std::unique_ptr<Sprite> fill_;  // 押している間に伸びる線

  float holdSec_ = 0.8f;    // 飛ばすまでに押し続ける時間（秒）
  float drainSec_ = 0.15f;  // 離したとき、伸びきった線が縮みきるまでの時間（秒）
  float fadeInSec_ = 0.15f; // 表示が現れきるまでの時間（秒）
  float fadeOutSec_ = 0.3f; // 表示が消えきるまでの時間（秒）
  float lingerSec_ = 1.2f;  // 離してから表示を消し始めるまでの時間（秒）
  float marginRight_ = 40.0f;  // 画面の右端から線の右端まで（px）
  float marginBottom_ = 40.0f; // 画面の下端から線まで（px）
  float lineGap_ = 6.0f;       // 文字の下端から線まで（px）
  float lineThickness_ = 2.0f; // 線の太さ（px）
  float trackAlpha_ = 0.25f;   // 線の下地の不透明度
  Vector4 lineColor_ = {0.94f, 0.84f, 0.59f, 1.0f}; // 線の色（淡い金色）

  float holdTimer_ = 0.0f;   // 押している時間（秒）。離すと縮む
  float lingerTimer_ = 0.0f; // 表示を残す残り時間（秒）
  float alpha_ = 0.0f;       // 表示の不透明度（0〜1）
  bool isArmed_ = false;     // いったん離したか（離すまで押しても数えない）
};
