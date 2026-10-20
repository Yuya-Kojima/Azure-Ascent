#pragma once
#include "Math/MathUtil.h"
#include <memory>

class Sprite;
class SpriteRenderer;

/// <summary>
/// ステージ開始演出の目標カード
/// 画面の上に横線が中央から伸び、線の上にステージ名、下に目標が出て、線が縮みながら消える
/// 文字は tools/IntroText/make_intro_text.py で書き出した画像
/// </summary>
class StageIntroText {
public:
  StageIntroText();
  ~StageIntroText();

  /// <summary>
  /// 初期化（文字の画像と横線のスプライトを作る）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 開始する（時刻を 0 に戻す）
  /// </summary>
  void Start();

  /// <summary>
  /// 止める（何も出さなくする。演出を飛ばしたときや Play をやめたとき）
  /// </summary>
  void Stop() { isActive_ = false; }

  /// <summary>
  /// 更新（時刻を進め、各スプライトの位置・大きさ・不透明度を決める）
  /// </summary>
  void Update();

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

  /// <summary>
  /// 調整用UI
  /// </summary>
  void DrawImGui();

private:
  std::unique_ptr<Sprite> line_;      // 横線
  std::unique_ptr<Sprite> stageName_; // ステージ名「第一空域」
  std::unique_ptr<Sprite> objective_; // 目標「天空を奪還せよ」

  // 開始してからの時刻（秒）
  float timer_ = 0.0f;
  bool isActive_ = false;

  //===========================
  // 調整値（時刻は開始してからの秒）
  //===========================
  float cardInTime_ = 2.75f;   // 横線が伸び始める時刻
  float lineGrowSec_ = 0.25f;  // 横線が伸びきるまでの時間
  float textInDelay_ = 0.25f;  // 横線が伸び始めてから文字が出始めるまでの時間
  float textInSec_ = 0.25f;    // 文字が出きるまでの時間
  float textRiseY_ = 8.0f;     // 文字が出るときに寄ってくる量（px）
  float cardOutTime_ = 4.2f;   // 消し始める時刻
  float cardOutSec_ = 0.3f;    // 消えきるまでの時間
  float lineY_ = 100.0f;       // 横線の高さ（画面上端から px）
  float lineWidth_ = 560.0f;   // 横線の長さ（px）
  float lineThickness_ = 2.0f; // 横線の太さ（px）
  float stageNameY_ = 72.0f;   // ステージ名の中心の高さ（px）
  float objectiveY_ = 150.0f;  // 目標の中心の高さ（px）
  Vector4 lineColor_ = {0.94f, 0.84f, 0.59f, 1.0f}; // 横線の色（淡い金色）
};
