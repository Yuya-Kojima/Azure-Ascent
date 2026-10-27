#pragma once
#include "Actor/DragonModel.h"
#include "Actor/DragonWingAnimator.h"
#include "Math/MathUtil.h"
#include <array>
#include <memory>
#include <string>

class Input;
class SkipHold;
class Sprite;
class SpriteRenderer;

/// <summary>
/// ゲームオーバーで拠点へ戻ったときの演出と UI
/// 雲が晴れると拠点の真上の空 → 伏せて休むドラゴンの右斜め後ろ・肩より少し高い所へ降りる
/// → 首を持ち上げるのに合わせてカメラも少し下がり、一緒に雲の天井を見上げる
/// → 見上げきって雲の天井に大きな雷が光ったあと、模様の光がかすかに戻る（傷は暗いまま）
/// → 空路の線（出撃からボスまでの距離のどこまで届いたか）と「再出撃」「拠点に残る」を出す
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
  /// <param name="progress">ボスまでの道のりのどこまで進んだか（0〜1。ボス戦まで来たら 1）</param>
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
  const DragonModel::Glow &GetGlow() const { return glow_; }
  const Vector3 &GetCameraPosition() const { return cameraPos_; }
  const Vector3 &GetCameraRotation() const { return cameraRot_; }

  /// <summary>
  /// 始めてからの時間（秒）
  /// </summary>
  float GetTime() const { return timer_; }

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

  // 今の時刻での、節目を通る曲線の上のオフセット（offset は節目のどのオフセットを使うか）
  Vector3 SampleCameraKeys(Vector3 CameraKey::*offset) const;

  // 調整値をキーと一緒に1つずつ visitor へ渡す（読み書きで同じ並びを使う）
  template <class Visitor> void VisitConfigValues(Visitor &&visit);
  void LoadConfig();
  void SaveConfig();

  void UpdateWake();
  void UpdateGlow();
  void UpdateCamera();
  void UpdateMenu(Input *input);
  void UpdateLeave();
  void UpdateSprites();

  // UI が出てから、選択肢が現れきって選べるようになるまでの時間（秒）
  float GetChoiceReadySec() const {
    return routeFadeSec_ + routeFillSec_ + choiceFadeSec_;
  }

  bool isActive_ = false;
  bool isPaused_ = false; // 時刻を止めているか（ImGui のつまみで時刻を動かして構図を見る）
  Phase phase_ = Phase::Wake;
  float timer_ = 0.0f;      // 始めてからの時間（秒）
  float phaseTimer_ = 0.0f; // 今の段階に入ってからの時間（秒）
  Choice pendingChoice_ = Choice::None;
  Choice focused_ = Choice::Retry;
  float progress_ = 0.0f; // ボスまでの道のりのどこまで進んだか（1 ならボス戦まで来た）

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
  DragonModel::Glow glow_;
  Vector3 cameraPos_{};
  Vector3 cameraRot_{};
  // 選んだ瞬間の姿勢の重みと翼とカメラ（ここから消す・戻す）
  DragonWingAnimator::Gesture leaveGesture_;
  DragonWingAnimator::State leaveWing_;
  Vector3 leaveCameraPos_{};
  Vector3 leaveCameraRot_{};

  //---- 時間 ----
  float lookUpTime_ = 2.5f;   // 首を持ち上げ始める時刻（秒）
  float lookUpSec_ = 1.2f;    // 首を持ち上げきるまでの時間（秒）。そのあとは見上げたまま止まる
  float menuTime_ = 4.4f;     // UI を出し始める時刻（秒）。大きな雷の残光が消えたあと
  float skipLockSec_ = 0.3f;  // 始めてから飛ばせるようになるまでの時間（秒）
  // UI が出てからの段取り（秒）: 空域名と暗い線が現れる → 水色の線が伸びる → 選択肢が現れる
  float routeFadeSec_ = 0.4f;  // 空域名と暗い線・印が現れきるまで
  float routeFillSec_ = 0.8f;  // 水色の線が出撃から落ちた所まで伸びきるまで
  float choiceFadeSec_ = 0.4f; // 選択肢が現れきるまで（ここから選べる）
  float markPopSec_ = 0.35f;   // 線が着いた所の印が、大きく光ってから落ち着くまで
  float focusMoveSec_ = 0.12f; // 選んでいる方の下線が、もう一方へ移りきるまで
  float leaveSec_ = 0.3f;     // 選んだあと、UI が消え、伏せて見上げる姿勢から立ち姿勢へ戻るまでの時間（秒）
  float stayCameraSec_ = 1.2f; // 拠点に残るとき、カメラがいつもの位置へ戻るまでの時間（秒）
  float glowReturnTime_ = 4.0f; // 模様の光が戻り始める時刻（秒）。大きな雷に応える
  float glowReturnSec_ = 0.6f;  // 模様の光が戻りきるまでの時間（秒）

  //---- ドラゴン ----
  // 伏せの姿勢で沈める深さ。伏せると腹の下端がモデルの単位で 0.134 上がるので、その分 × 大きさ 1.7
  float lieDepth_ = 0.23f;
  float chestRaise_ = 0.2f; // 見上げきったときに胸を起こす量（0: 首と頭だけ 〜 1: 前脚で胸を起こす）
  float wingMotion_ = 0.3f; // 見上げきったあとの翼の動き（0: 動かない 〜 1: 地面の上で小さく打つ）
  float stirSec_ = 1.5f;    // 見上げきってから、翼の動きが強まりきるまでの時間（秒）
  // 羽ばたき（強さ, 1秒あたりの回数, 畳み具合, 左右差, 姿勢, 羽ばたきの姿勢の割合）
  // 伏せの姿勢の翼（地面へ垂らす）をそのまま使うので、畳まない
  DragonWingAnimator::State lieWing_ = {0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f};
  DragonWingAnimator::State stirWing_ = {0.08f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f}; // 翼の動きが 1 のとき（地面の上で小さく動く）
  DragonWingAnimator::State standWing_ = {0.05f, 0.4f, 0.5f, 0.0f, 0.0f, 0.0f}; // 選んだあとの立ち姿勢
  // 戻りきった模様の光の強さ（ゲーム中の 0.25 より弱い）。傷は開いたまま光らない
  float returnPattern_ = 0.1f;

  //---- カメラ ----
  // 最初の節目を出るときの速さ（最初の区間の平均の速さに対する倍率。3 を超えると行き過ぎることがある）
  float descentStartSpeed_ = 1.5f;
  bool isCameraMirrored_ = false; // カメラの節目の右を左に入れ替える（ドラゴンの左側から撮る）
  std::array<CameraKey, 3> cameraKeys_ = {{
      {0.6f, {2.0f, 28.0f, -3.0f}, {0.0f, 0.0f, 0.0f}},     // 拠点の真上の空
      {2.5f, {2.4f, 1.6f, -3.4f}, {0.0f, 0.35f, 0.6f}},     // 右斜め後ろ・肩より少し高い所から、伏せた体を見下ろす
      {3.7f, {2.1f, 0.6f, -3.6f}, {-0.66f, 1.32f, 4.94f}},  // 肩の高さへ下がり、5 度見上げる（ドラゴンは画面の左下、視線の先の右上が雲の天井）
  }};

  //---- UI（画面の位置は 1280x720 の px。文字の位置は、画像の縁の余白を除いた文字の左端）----
  float menuLeft_ = 820.0f;   // 空路の線の左端（出撃の点）。画面の右下（左下はドラゴン）
  // UI の後ろに敷く暗い板（右端は画面の右端。上・左・下の縁に金の細い線）
  float plateLeft_ = 765.0f;
  float plateTop_ = 375.0f;
  float plateHeight_ = 304.0f;
  float plateAlpha_ = 0.9f;
  float titleY_ = 420.0f;     // 空域名の縦の中心
  float routeY_ = 462.0f;     // 空路の線の縦の位置
  float routeWidth_ = 380.0f; // 空路の線の長さ（出撃 〜 ボス）
  float routeLabelY_ = 492.0f; // 線の下の名前（出撃・ボス）の縦の中心
  float retryY_ = 556.0f;     // 「再出撃」の縦の中心
  float stayY_ = 618.0f;      // 「拠点に残る」の縦の中心
  float underlineOffset_ = 26.0f; // 選択肢の縦の中心から、下線までの距離
  float unfocusedBrightness_ = 1.0f;  // 選んでいない方の明るさ
  float unfocusedAlpha_ = 0.6f;       // 選んでいない方の不透明度

  std::unique_ptr<Sprite> plate_;      // UI の後ろの暗い板
  std::array<std::unique_ptr<Sprite>, 3> plateEdges_; // 板の上・左・下の縁の線
  std::unique_ptr<Sprite> title_;      // 空域名
  std::unique_ptr<Sprite> routeTrack_; // 出撃 〜 ボスの暗い線
  std::unique_ptr<Sprite> routeGlow_;  // 水色の線のまわりの光
  std::unique_ptr<Sprite> routeCore_;  // 水色の線の芯
  std::unique_ptr<Sprite> startMark_;  // 出撃の点
  std::unique_ptr<Sprite> reachMark_;  // 落ちた所のひし形
  std::unique_ptr<Sprite> bossMark_;   // ボスのひし形
  std::unique_ptr<Sprite> startLabel_;
  std::unique_ptr<Sprite> bossLabel_;
  std::unique_ptr<Sprite> retry_;
  std::unique_ptr<Sprite> stay_;
  std::unique_ptr<Sprite> underline_;  // 選んでいる方の下線
  std::unique_ptr<SkipHold> skipHold_; // 起き上がりを飛ばす長押し
  float leaveAlpha_ = 1.0f; // 選んだあと UI を消していく不透明度
  float focusPos_ = 0.0f;   // 下線の位置（0: 再出撃 〜 1: 拠点に残る）
};
