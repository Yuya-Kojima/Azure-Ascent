#pragma once
#include "Core/EngineBase.h"
#include "Math/Vector3.h"
#include "Scene/BaseScene.h"
#include <memory>
#include <string>
#include <vector>

class DebugCamera;
class Enemy;
class GameCamera;
class Object3d;
class Skybox;
class StageLighting;

/// <summary>
/// 雑魚敵（A 単眼・B くさび）の見え方の確認用シーン
/// ゲームと同じ画角・光・空の前に、プレハブから作った敵を画面での大きさを決めて並べる
/// 体・光・ふちの光の色と強さをその場で変え、プレハブに保存できる
/// </summary>
class ZakoViewScene : public BaseScene {

public: // メンバ関数
  // unique_ptr<Enemy> などを前方宣言のまま持つため、コンストラクタとデストラクタは .cpp で定義する
  ZakoViewScene();
  ~ZakoViewScene() override;

  void Initialize(EngineBase *engine) override;
  void Finalize() override;
  void Update() override;
  void Draw() override;
  void Draw2D() override;
  void Draw3D() override;

private: // 型
  // 並べ方
  enum class Layout {
    kSideBySide, // A と B を 1 体ずつ横に並べる
    kEyeRing,    // A を 8 体の輪にする
    kArrowRow,   // B を 1 列にして左へ流す
  };

  // 並べ方が「A と B を並べる」のときの B の向き（カメラから見て）
  enum class ArrowFacing {
    kFront,  // 正面（鼻先がこちら）
    kSide,   // 真横
    kTop,    // 真上（背中がこちら）
    kAngled, // 斜め前の上
    kSpin,   // 回し続ける
  };

  // 体・光・ふちの光の見た目（A と B で共通）
  struct Look {
    Vector3 bodyColor = {0.12f, 0.14f, 0.18f};
    Vector3 rimColor = {0.485f, 0.565f, 0.672f};
    float rimIntensity = 1.0f;
    float rimPower = 2.5f;
    Vector3 lightColor = {1.0f, 0.102f, 0.023f};
    float lightIntensity = 3.0f;
    bool whiteHitFlash = false; // 被弾の色を白にするか（false で赤）
  };

  // 並べた敵1体分
  struct Placed {
    std::unique_ptr<Enemy> enemy = nullptr;
    Vector3 offset = {0.0f, 0.0f, 0.0f}; // 並べ方の中心からの位置
    bool isArrow = false;                // B くさびか
  };

private: // メンバ関数
  /// <summary>
  /// 今の並べ方で敵を作り直す
  /// </summary>
  void Respawn();

  /// <summary>
  /// 画面での大きさ（ピクセル）から、敵を置く距離を求める
  /// </summary>
  float GetDistance() const;

  /// <summary>
  /// 敵の位置・向き・見た目を今の設定にする
  /// </summary>
  void ApplyToEnemies();

  /// <summary>
  /// プレハブ ZakoEye から見た目の初期値を読む
  /// </summary>
  void LoadLook();

  /// <summary>
  /// 見た目を ZakoEye・ZakoArrow の両方のプレハブに書き込む（ほかの項目は残す）
  /// </summary>
  void SaveLook();

  /// <summary>
  /// 調整用の窓
  /// </summary>
  void DrawDebugUI();

private: // メンバ変数
  // ゲームと同じ画角のカメラ（原点から +Z を見る）
  std::unique_ptr<GameCamera> camera_ = nullptr;

  // まわりを見るための自由カメラ（右ドラッグで回転、WASDで移動。画角はゲームと違う）
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;
  bool useDebugCamera_ = false;

  // 空と、空・光・霧・ポストエフェクトの設定（ゲームと同じ設定ファイルを読む）
  std::unique_ptr<Skybox> skybox_ = nullptr;
  std::unique_ptr<StageLighting> lighting_ = nullptr;
  float skyTime_ = 0.0f;

  // 敵の後ろに置く背景の板（ライティングなしで色をそのまま出す）
  std::unique_ptr<Object3d> backdrop_ = nullptr;
  bool showBackdrop_ = true;
  Vector3 backdropColor_ = {0.0f, 0.0f, 0.0f};

  // 並べた敵
  std::vector<Placed> placed_;

  // 並べ方と、画面での大きさ（敵の 6m がこのピクセル数に映る距離に置く）
  Layout layout_ = Layout::kSideBySide;
  float targetPixels_ = 65.0f;

  // B の向き（並べ方が「A と B を並べる」のとき）と、回し続けるときの今の角度
  ArrowFacing arrowFacing_ = ArrowFacing::kSide;
  float spinAngle_ = 0.0f;

  // B の列を左へ流す速さ（1秒あたりの距離）と、流した量
  float rowSpeed_ = 20.0f;
  float rowShift_ = 0.0f;

  // 被弾の点滅を一定の間隔でくり返すか、と経過フレーム
  bool repeatHitFlash_ = false;
  int hitFlashFrame_ = 0;

  // 体・光・ふちの光の見た目
  Look look_;

  // 保存の結果の表示
  std::string saveMessage_;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
