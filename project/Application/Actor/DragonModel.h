#pragma once
#include "Actor/DragonWingAnimator.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <memory>

class Object3d;
class Object3dRenderer;
class SkinnedObject;
class SrvManager;

/// <summary>
/// 自機ドラゴンの見た目。ボーン付きモデルと羽ばたきをまとめ、使う側は状態を渡して Update・Draw するだけにする
/// </summary>
class DragonModel {
public:
  DragonModel();
  ~DragonModel();

  /// <summary>
  /// 初期化（モデルの読み込み、鏡面反射の強さ、羽ばたきの準備）
  /// </summary>
  void Initialize(Object3dRenderer *object3dRenderer, SrvManager *srvManager);

  /// <summary>
  /// 羽ばたきの目標の状態（今の状態はここへ滑らかに近づく）
  /// </summary>
  void SetWingState(const DragonWingAnimator::State &state);

  /// <summary>
  /// 補間せずに、羽ばたきの状態を切り替える
  /// </summary>
  void SetWingStateImmediate(const DragonWingAnimator::State &state);

  /// <summary>
  /// 羽ばたきを進め、関節とワールド行列を更新する。deltaTime 0 で羽ばたきを止めたまま更新する
  /// </summary>
  void Update(float deltaTime);

  void Draw();

  /// <summary>
  /// 位置・向き・大きさの設定に使う Object3d
  /// </summary>
  Object3d *GetObject3d() const;

  /// <summary>
  /// 翼端のワールド座標（最後の Update の時点）
  /// </summary>
  Vector3 GetLeftWingTip() const;
  Vector3 GetRightWingTip() const;

  /// <summary>
  /// 頭の骨（Head_3）の根元のワールド座標（最後の Update の時点）
  /// </summary>
  Vector3 GetHeadPosition() const;

  DragonWingAnimator &GetWingAnimator() { return wing_; }

  /// <summary>
  /// 竜の力の光（模様の光と傷）の見た目と、今の強さ。長さはモデルの単位（大きさ 1 のときの長さ）
  /// </summary>
  struct Glow {
    Vector3 patternColor = {0.35f, 1.0f, 0.85f}; // 模様の光の色
    float pattern = 0.25f;                       // 模様の光の強さ（0 で光らない）
    float patternLow = 0.45f;  // 光り始める水色らしさ（min(緑, 青) − 赤。リニアの色）
    float patternHigh = 0.6f;  // 光りきる水色らしさ
    float woundOpen = 0.0f;    // 傷の開いた割合（0 で傷なし、1 で開ききる）
    float woundGlow = 0.0f;    // 傷から漏れる光の強さ（0 で穴とひびは暗いまま）
    Vector3 woundColor = {0.05f, 1.0f, 0.75f}; // 傷から漏れる光の色
    float woundRadius = 0.035f;  // 穴の半径
    float crackLength = 0.09f;   // ひびの長さ（中心から）
    float crackWidth = 0.006f;   // ひびの根元の太さ
    float rimWidth = 0.35f;      // 縁の欠けた殻の幅（穴の半径に対する割合）
    float rimJag = 0.25f;        // 縁のぎざぎざの深さ（穴の半径に対する割合）
    float spill = 0.3f;          // 穴の周りの面ににじむ光の強さ
  };

  /// <summary>
  /// 竜の力の光を設定する（次の Update で反映）
  /// </summary>
  void SetGlow(const Glow &glow) { glow_ = glow; }

private:
  // 傷の場所と向き（ワールド。最後の Update の時点）
  struct WoundFrame {
    Vector3 center{};                     // 穴の中心（体の表面）
    Vector3 normal = {0.0f, 1.0f, 0.0f};  // 外向き
    Vector3 tangent = {0.0f, 0.0f, 1.0f}; // 面に沿った、体の前の向き
    float scale = 1.0f; // モデルの大きさ（モデルの単位の長さをワールドの長さにする倍率）
  };

  Vector3 GetWingTip(int32_t jointIndex) const;

  // このフレームのワールド行列を、Object3d の設定と親から求める（Object3d の Update より前に使う）
  Matrix4x4 ComputeWorldMatrix() const;

  // 傷の場所と向きを、体の表面の頂点に合わせて求め、模様の光と傷をマテリアルへ渡す
  void UpdateGlow();

  std::unique_ptr<SkinnedObject> skinned_;
  DragonWingAnimator wing_;
  int32_t leftTipJoint_ = -1;  // 左の翼端の指の最後の骨
  int32_t rightTipJoint_ = -1; // 右の翼端の指の最後の骨
  int32_t headJoint_ = -1;     // 頭の骨

  Glow glow_;
  WoundFrame woundFrame_;
  uint32_t woundVertex_ = 0; // 傷の中心に使う体の表面の頂点
};
