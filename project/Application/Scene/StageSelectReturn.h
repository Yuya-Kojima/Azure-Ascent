#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <array>
#include <memory>

class Input;
class Sprite;
class SpriteRenderer;

/// <summary>
/// ゲームオーバーで拠点へ戻ったときの演出と UI
/// 雲が晴れると拠点の真上の空 → 倒れたドラゴンへ降りていく → 頭を上げ、カメラは斜め後ろへ回り込む
/// → 起き上がり、カメラは背中の後ろへ → 首を立てて暗い空を見上げ、カメラは背中越しに空を見上げる
/// → 到達度と「再出撃」「拠点に残る」を出す
/// 位置はすべて、拠点のドラゴンの位置と向き（水平な右・上・前）を基準に求める
/// </summary>
class StageSelectReturn {
public:
  /// <summary>
  /// UI で選んだもの
  /// </summary>
  enum class Choice {
    None,  // まだ選んでいない
    Retry, // 再出撃
    Stay,  // 拠点に残る
  };

  StageSelectReturn();
  ~StageSelectReturn();

  /// <summary>
  /// 初期化（UI の画像を読む）
  /// </summary>
  void Initialize(SpriteRenderer *spriteRenderer);

  /// <summary>
  /// 演出を始める
  /// </summary>
  /// <param name="dragonPos">拠点で立っているときのドラゴンの位置</param>
  /// <param name="dragonRot">拠点で立っているときのドラゴンの向き</param>
  /// <param name="baseCameraPos">いつもの拠点のカメラの位置（拠点に残るとき、ここへ戻る）</param>
  /// <param name="baseCameraRot">いつもの拠点のカメラの向き</param>
  /// <param name="progress">ステージの到達度（0〜1）</param>
  void Start(const Vector3 &dragonPos, const Vector3 &dragonRot,
             const Vector3 &baseCameraPos, const Vector3 &baseCameraRot,
             float progress);

  /// <summary>
  /// 更新
  /// </summary>
  /// <param name="input">操作を受けるときの入力。nullptr なら操作を受けない</param>
  void Update(Input *input);

  /// <summary>
  /// 演出中か（拠点に残ると決めて、カメラがいつもの位置へ戻りきると false）
  /// </summary>
  bool IsActive() const { return isActive_; }

  /// <summary>
  /// 選んだもの（一度だけ返し、そのあとは None）
  /// </summary>
  Choice ConsumeChoice();

  /// <summary>
  /// ドラゴンの位置・向きをこの演出が決めているか（再出撃を選ぶと、出撃演出に任せる）
  /// </summary>
  bool IsDrivingDragon() const;

  const Vector3 &GetDragonPosition() const { return dragonPos_; }
  const Vector3 &GetDragonRotation() const { return dragonRot_; }
  const DragonWingAnimator::State &GetWingState() const { return wingState_; }
  const DragonWingAnimator::Gesture &GetGesture() const { return gesture_; }
  const Vector3 &GetCameraPosition() const { return cameraPos_; }
  const Vector3 &GetCameraRotation() const { return cameraRot_; }

  /// <summary>
  /// UI が出始めたか（BGM を流し始める合図に使う）
  /// </summary>
  bool IsMenuShown() const;

  /// <summary>
  /// UI の描画
  /// </summary>
  void Draw();

  /// <summary>
  /// 調整値の ImGui
  /// </summary>
  void DrawImGui();

private:
  // カメラの節目（ドラゴンからの右・上・前のオフセット）
  struct CameraKey {
    float time = 0.0f; // この節目に着く時刻（秒）
    Vector3 cameraOffset{};
    Vector3 lookOffset{};
  };

  // 演出の段階
  enum class Phase {
    Wake,  // 降りる・起き上がる・見上げる
    Menu,  // UI で選ぶ
    Retry, // 再出撃を選んだ（UI と見上げる姿勢を消す）
    Stay,  // 拠点に残るを選んだ（カメラをいつもの位置へ戻す）
  };

  // ドラゴンの位置から、ドラゴンの水平な右・上・前の向きで offset だけ動かした位置
  Vector3 ToWorld(const Vector3 &offset) const;

  void UpdateWake();
  void UpdateCamera();
  void UpdateMenu(Input *input);
  void UpdateLeave();
  void UpdateSprites();

  bool isActive_ = false;
  bool isPaused_ = false; // 時刻を止めているか（ImGui のつまみで時刻を動かして構図を見る）
  Phase phase_ = Phase::Wake;
  float timer_ = 0.0f;      // 始めてからの時間（秒）
  float phaseTimer_ = 0.0f; // 今の段階に入ってからの時間（秒）
  Choice pendingChoice_ = Choice::None;
  Choice focused_ = Choice::Retry;
  float progress_ = 0.0f;

  // 基準
  Vector3 basePos_{};
  Vector3 baseRot_{};
  Vector3 forward_ = {0.0f, 0.0f, 1.0f};
  Vector3 right_ = {1.0f, 0.0f, 0.0f};
  Vector3 baseCameraPos_{};
  Vector3 baseCameraRot_{};

  // 今のドラゴン・カメラ
  Vector3 dragonPos_{};
  Vector3 dragonRot_{};
  DragonWingAnimator::State wingState_;
  DragonWingAnimator::Gesture gesture_;
  Vector3 cameraPos_{};
  Vector3 cameraRot_{};
  // 選んだ瞬間の見上げる重みとカメラ（ここから消す・戻す）
  float leaveLookUp_ = 0.0f;
  Vector3 leaveCameraPos_{};
  Vector3 leaveCameraRot_{};

  //---- 時間 ----
  float headUpTime_ = 1.5f;   // 頭を上げ始める時刻（秒）
  float riseTime_ = 2.5f;     // 起き上がり始める時刻（秒）
  float lookUpTime_ = 3.5f;   // 見上げ始める時刻（秒）
  float menuTime_ = 4.5f;     // UI を出し始める時刻（秒）
  float skipLockSec_ = 0.3f;  // 始めてから飛ばせるようになるまでの時間（秒）
  float menuFadeSec_ = 0.6f;  // UI が現れきるまでの時間（秒）
  float barFillSec_ = 0.8f;   // 到達度のバーが伸びきるまでの時間（UI が現れてから、秒）
  float leaveSec_ = 0.3f;     // 選んだあと、UI と見上げる姿勢が消えるまでの時間（秒）
  float stayCameraSec_ = 1.2f; // 拠点に残るとき、カメラがいつもの位置へ戻るまでの時間（秒）

  //---- ドラゴン ----
  float lieDepth_ = 0.35f;  // 倒れているときに沈める深さ
  float liePitch_ = 0.3f;   // 倒れているときの機首の下がり（ラジアン）
  float lieRoll_ = 0.25f;   // 倒れているときの横の傾き（ラジアン）
  float headUpCollapse_ = 0.4f; // 頭を上げきったときの、倒れる姿勢の重み
  // 羽ばたき（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）
  DragonWingAnimator::State lieWing_ = {0.0f, 0.4f, 1.0f, 0.0f, 0.0f, 0.0f};   // 倒れている
  DragonWingAnimator::State riseWing_ = {0.15f, 0.8f, 0.3f, 0.0f, 0.0f, 0.0f}; // 翼を少し開いて支える
  DragonWingAnimator::State standWing_ = {0.05f, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f}; // 見上げる

  //---- カメラ ----
  std::array<CameraKey, 5> cameraKeys_ = {{
      {0.0f, {2.0f, 28.0f, -3.0f}, {0.0f, 0.0f, 0.0f}}, // 拠点の真上の空
      {1.5f, {4.5f, 2.2f, 2.5f}, {0.0f, 0.6f, 0.0f}},   // 倒れたドラゴンの横
      {2.5f, {3.0f, 1.8f, -3.5f}, {0.0f, 1.0f, 0.5f}},  // 斜め後ろ
      {3.5f, {1.2f, 1.5f, -4.5f}, {0.0f, 1.5f, 2.0f}},  // 背中の後ろ
      {4.5f, {0.9f, 0.3f, -7.0f}, {0.0f, 3.3f, 10.0f}}, // 背中越しに空を見上げる
  }};

  //---- UI（画面の位置は 1280x720 の px）----
  float menuLeft_ = 110.0f;      // UI の左端
  float labelY_ = 430.0f;        // 「到達度」の縦の中心
  float digitsLeft_ = 330.0f;    // 数字の左端
  float digitAdvance_ = 30.0f;   // 数字1文字ぶん進める幅
  float barY_ = 478.0f;          // バーの縦の中心
  float barWidth_ = 360.0f;
  float barHeight_ = 8.0f;
  float retryY_ = 560.0f;        // 「再出撃」の縦の中心
  float stayY_ = 640.0f;         // 「拠点に残る」の縦の中心
  float choiceScale_ = 0.8f;     // 選択肢の画像の大きさ
  float markerWidth_ = 8.0f;     // 選んでいる方の印の幅
  float markerGap_ = 18.0f;      // 印と選択肢の間
  float unfocusedAlpha_ = 0.45f; // 選んでいない方の不透明度

  std::unique_ptr<Sprite> label_;
  std::array<std::unique_ptr<Sprite>, 4> digits_; // 「100%」まで
  std::unique_ptr<Sprite> barBack_;
  std::unique_ptr<Sprite> barFill_;
  std::unique_ptr<Sprite> retry_;
  std::unique_ptr<Sprite> stay_;
  std::unique_ptr<Sprite> marker_;
  int digitCount_ = 0;  // 描く数字の数
  float uiAlpha_ = 0.0f;
};
