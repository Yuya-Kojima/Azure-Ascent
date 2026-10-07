#pragma once
#include "Audio/SoundManager.h"
#include "Core/EngineBase.h"
#include "Math/MathUtil.h"
#include "Math/Transform.h"
#include "Scene/BaseScene.h"
#include <deque>
#include <vector>

class Object3d;
class DebugCamera;
class GameCamera;
class Skybox;

class TitleScene : public BaseScene {

private: // メンバ変数(ゲーム用)
  // スカイボックス
  std::unique_ptr<Skybox> skybox_ = nullptr;

  // 3Dオブジェクト
  std::unique_ptr<Object3d> dragonObject_ = nullptr;
  std::unique_ptr<Object3d> cloudsObject_ = nullptr;
  std::unique_ptr<Object3d> cloudsObjectFar_ = nullptr;

  // ドラゴン配置・モーション用パラメータ
  Transform dragonTransform_{};
  Vector3 baseDragonPos_ = {0.0f, 0.0f, 0.0f};
  Vector3 baseDragonRot_ = {0.0f, 0.0f, 0.0f};
  float motionTimer_ = 0.0f;
  float openingTimer_ = 0.0f; // 起動時オープニング演出用タイマー
  const float kOpeningDuration_ = 3.0f; // オープニングのカメラ演出秒数（終了時にカット1へ完全一致）
  const float kLogoLeadTime_ = 0.7f;    // オープニング終了の何秒前からタイトルUIを出し始めるか
  const float kTitleIntroDuration_ = 0.95f;    // タイトルロゴがズドンと決まるまでの時間(秒)
  const float kStartTextFadeDuration_ = 0.60f; // PRESS ENTER テキストが出現完了するまでの時間(秒)
  const float kRearWideDriftY_ = 0.18f; // カット1のカメラYドリフト振幅（オープニング終端の初期値と共通）

  // 連続飛行の調整用パラメータ
  const float kWeaveStartTime_ = 1.6f;  // 蛇行（8の字）が育ち始める時刻(秒)。それまでは真っ直ぐ頭上通過
  const float kWeaveRampTime_ = 2.4f;   // 蛇行が完全に育ち切るまでの時間(秒)

  // 自機の動きに変化をつけるパラメータ（単調に見せないため）
  const float kTempoPeriod_ = 17.0f;    // 蛇行の速さが緩急する周期(秒)
  const float kTempoVariation_ = 0.15f; // 蛇行の速さの変化幅（0.15で±15%）
  const float kAmpPeriod_ = 14.0f;      // 蛇行の振幅が大小する周期(秒)
  const float kAmpVariation_ = 0.35f;   // 蛇行の振幅の変化幅（0.35で±35%）
  const float kEventFirstTime_ = 10.0f; // 最初の見せ場の時刻(秒)
  const float kEventInterval_ = 22.0f;  // 見せ場の間隔(秒)
  const float kEventDuration_ = 5.0f;   // 見せ場1回の長さ(秒)
  const float kEventRise_ = 0.8f;       // 見せ場で上がる高さ
  const float kEventBank_ = 0.5f;       // 見せ場のバンク角(rad)

  // 自機の登場（後方の画面外 → 頭上通過）とカメラの予備動作
  const float kEntryStartZ_ = -9.0f;       // 自機の開始Z（後方の画面外）
  const float kEntryDuration_ = 1.8f;      // 開始Zから定位置へ減速しながら入る時間(秒)
  const float kCamTiltStartTime_ = 0.5f;   // カメラが引き（傾き）を始める時刻(秒)。通過の少し前
  const float kCamFollowStartTime_ = 0.7f; // 自機の頭上通過の時刻。ここからカメラのZ追従を始める
  const float kCamFollowBlendTime_ = 0.9f; // カメラのZ追従が効き切るまでの時間(秒)
  const float kLookTrackStartTime_ = 0.5f; // 注視点が自機へ寄り始める時刻(秒)
  const float kLookTrackBlendTime_ = 0.6f; // 注視点の寄せが効き切るまでの時間(秒)
  const float kLookTrackMax_ = 0.6f;       // 注視点を自機へ寄せる最大割合(0〜1)
  // カメラの追従の遅れ(秒)。大きいほど慣性が強い。
  // オープニング中は自機が画面外に出ないよう小さく、終了後に大きくして慣性を効かせる。
  const float kCamPosSmoothOpening_ = 0.12f;
  const float kCamLookSmoothOpening_ = 0.10f;
  const float kCamPosSmoothCruise_ = 0.30f;
  const float kCamLookSmoothCruise_ = 0.35f;
  const float kCamSmoothRampTime_ = 1.5f; // オープニング終了後、遅れが大きくなり切るまでの時間(秒)

  // カット1/オープニング共通の慣性カメラ状態
  Vector3 camSmoothPos_{};
  Vector3 camSmoothLook_{};
  Vector3 camSmoothPosVel_{};
  Vector3 camSmoothLookVel_{};
  bool camSmoothValid_ = false;
  float uiIntroTimer_ = 0.0f;  // タイトルUIの登場演出用タイマー
  bool isBgmStarted_ = false;  // タイトルBGM再生開始フラグ
  float loopFadeAlpha_ = 0.0f; // 放置120秒ループ時の暗転フェード用アルファ
  bool isStartWindPlayed_ = false; // 発進風切りSE再生済みフラグ

  // 雲海スクロール
  float cloudsScrollZ_ = 0.0f;

  // 時間帯（昼→午後→夕焼け を循環）
  float skyTimer_ = 0.0f;
  // ImGuiからの露出・ブルーム強度の微調整（UpdateSkyが毎フレーム値を計算するためオフセットで指定）
  float exposureOffset_ = 0.0f;
  float bloomIntensityOffset_ = 0.0f;

  // シネマティックカメラ管理
public:
  enum class TitleCameraCut {
    RearWide,        // カット1: 後方ワイド追従
    FrontTracking,   // カット2: 斜め前方並走
    LowAngle,        // カット3: 下からのあおり
    DistantOverlook, // カット4: 遠景の俯瞰（ロングショット）
    WingtipCloseUp,  // カット5: 翼端トレイルクローズアップ
  };

private:
  TitleCameraCut currentCut_ = TitleCameraCut::RearWide;
  float cutTimer_ = 0.0f;
  const float kCutDuration_ = 7.0f; // 各カットの持続秒数
  bool isManualCut_ = false;        // デバッグ手動固定フラグ
  Vector3 currentLightDir_{0.55f, -0.45f, 0.65f}; // カット別ライト向きの現在値

  // 翼端トレイル履歴
  std::deque<Vector3> rightTrailHistory_;
  std::deque<Vector3> leftTrailHistory_;
  const size_t kMaxTrailHistory_ = 60;
  float trailWidth_ = 0.06f;
  float trailAlpha_ = 0.75f;
  float trailWindShift_ = 0.25f;
  bool enableTrail_ = true;

  // 気流・風ストリークパーティクル
  struct WindStreak {
    Vector3 pos;
    float baseLength;
    float speed;
    float alpha;
    float width;
  };
  std::vector<WindStreak> windStreaks_;
  bool enableWind_ = true;
  float windSpeedMult_ = 1.0f;
  float windAlpha_ = 0.70f;
  float windLengthMult_ = 1.0f;
  const size_t kMaxWindStreaks_ = 36;

  // ゲームスタート発進演出
  bool isStarting_ = false;
  float startTimer_ = 0.0f;
  const float kStartDuration_ = 1.3f;
  TitleCameraCut startCut_ = TitleCameraCut::RearWide;
  Vector3 startCamPos_{};
  Vector3 startCamRot_{};
  float startCamFov_ = 0.70f;
  Vector3 startDragonPos_{};
  Vector3 startDragonRot_{};
  bool hasTransitioned_ = false;

public:  // メンバ関数
  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(EngineBase *engine) override;

  /// <summary>
  /// 終了
  /// </summary>
  void Finalize() override;

  /// <summary>
  /// 更新
  /// </summary>
  void Update() override;

  /// <summary>
  /// 描画
  /// </summary>
  void Draw() override;

  /// <summary>
  /// 2Dオブジェクト描画
  /// </summary>
  void Draw2D() override;

  /// <summary>
  /// 3Dオブジェクト描画
  /// </summary>
  void Draw3D() override;

  /// <summary>
  /// エディタUI描画
  /// </summary>
  void DrawEditorUI() override;

private: // メンバ変数(システム用)
         // カメラ
  std::unique_ptr<GameCamera> camera_ = nullptr;

  // デバッグカメラ
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // デバッグカメラ使用
  bool useDebugCamera_ = false;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;

private: // 更新サブ処理（パイプライン用プライベート関数）
  /// <summary>
  /// 注視回転角（ピッチ・ヨー）の計算ヘルパー
  /// </summary>
  static Vector3 CalcLookAtRot(const Vector3 &eye, const Vector3 &target);

  /// <summary>
  /// 入力・UIの更新（呼吸アニメーション・フェード）
  /// </summary>
  void UpdateUI();

  /// <summary>
  /// 発進演出の更新（カメラ・ドラゴンの加速、ポストプロセス連動）
  /// </summary>
  void UpdateLaunchSequence(Vector3 &outTargetCamPos, Vector3 &outTargetCamRot, float &outLaunchAccelCurve);

  /// <summary>
  /// 通常鑑賞モードの更新（待機飛行モーション、雲海スクロール、カット巡回）
  /// </summary>
  void UpdateIdleMotion(Vector3 &outTargetCamPos, Vector3 &outTargetCamRot);

  /// <summary>
  /// カメラカット連動ライティングの更新
  /// </summary>
  void UpdateLighting();

  /// <summary>
  /// カメラの更新とアクティブカメラの確定
  /// </summary>
  const ICamera *UpdateActiveCamera(const Vector3 &targetCamPos, const Vector3 &targetCamRot);

  /// <summary>
  /// 3Dオブジェクト（ドラゴン・雲海）の更新とDoF調整
  /// </summary>
  void Update3DObjects(const ICamera *activeCamera);

  /// <summary>
  /// 翼端トレイルの更新と描画登録
  /// </summary>
  void UpdateWingTrails(const ICamera *activeCamera, float launchAccelCurve);

  /// <summary>
  /// 気流・風ストリークの更新と描画登録
  /// </summary>
  void UpdateWindStreaks(const ICamera *activeCamera);

  /// <summary>
  /// 時間帯（空・太陽・フォグ・ライト色・雲海色）の更新
  /// </summary>
  void UpdateSky(const ICamera *activeCamera);
};
