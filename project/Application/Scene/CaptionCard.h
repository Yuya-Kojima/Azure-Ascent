#pragma once
#include "Math/MathUtil.h"
#include <memory>
#include <string>

class Sprite;
class SpriteRenderer;

/// <summary>
/// 文字カード（ステージ開始演出の目標・ゲームオーバー）
/// 横線が中央から伸び、線の上と下に文字の画像が線へ寄りながら出て、線が縮みながら消える
/// 文字は tools/IntroText/make_intro_text.py で書き出した画像
/// </summary>
class CaptionCard {
public:
  /// <summary>
  /// 出す文字と時刻・置き場所（時刻は開始してからの秒、位置は 1280x720 の px）
  /// </summary>
  struct Settings {
    std::string upperPath; // 線の上の文字の画像
    std::string lowerPath; // 線の下の文字の画像
    float cardInTime = 0.0f;    // 横線が伸び始める時刻
    float lineGrowSec = 0.25f;  // 横線が伸びきるまでの時間
    float textInDelay = 0.25f;  // 横線が伸び始めてから文字が出始めるまでの時間
    float textInSec = 0.25f;    // 文字が出きるまでの時間
    float textRiseY = 8.0f;     // 文字が出るときに寄ってくる量（px）
    float cardOutTime = 1.5f;   // 消し始める時刻
    float cardOutSec = 0.3f;    // 消えきるまでの時間
    float lineY = 100.0f;       // 横線の高さ（画面上端から px）
    float lineWidth = 560.0f;   // 横線の長さ（px）
    float lineThickness = 2.0f; // 横線の太さ（px）
    float upperY = 72.0f;       // 線の上の文字の中心の高さ（px）
    float lowerY = 150.0f;      // 線の下の文字の中心の高さ（px）
    Vector4 lineColor = {0.94f, 0.84f, 0.59f, 1.0f}; // 横線の色（淡い金色）
  };

  CaptionCard();
  ~CaptionCard();

  /// <summary>
  /// 初期化（文字の画像と横線のスプライトを作る）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer, const Settings &settings);

  /// <summary>
  /// 開始する（時刻を 0 に戻す）
  /// </summary>
  void Start();

  /// <summary>
  /// 止める（何も出さなくする。演出を飛ばしたときや Play をやめたとき）
  /// </summary>
  void Stop() { isActive_ = false; }

  /// <summary>
  /// 更新（時刻を 1 フレーム進め、各スプライトの位置・大きさ・不透明度を決める）
  /// </summary>
  void Update();

  /// <summary>
  /// 時刻を time にして、各スプライトの位置・大きさ・不透明度を決める
  /// （演出の側の時計に合わせるとき。消えきる前の時刻なら出す状態に戻る）
  /// </summary>
  void SetTime(float time);

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

  /// <summary>
  /// 調整用UI
  /// </summary>
  void DrawImGui();

  /// <summary>
  /// 消えきる時刻（開始してからの秒）
  /// </summary>
  float GetEndTime() const {
    return settings_.cardOutTime + settings_.cardOutSec;
  }

private:
  // 今の時刻で、各スプライトの位置・大きさ・不透明度を決める
  void Apply();

  std::unique_ptr<Sprite> line_;  // 横線
  std::unique_ptr<Sprite> upper_; // 線の上の文字
  std::unique_ptr<Sprite> lower_; // 線の下の文字

  Settings settings_;

  // 開始してからの時刻（秒）
  float timer_ = 0.0f;
  bool isActive_ = false;
};
