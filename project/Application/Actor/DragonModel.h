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

private:
  Vector3 GetWingTip(int32_t jointIndex) const;

  // このフレームのワールド行列を、Object3d の設定と親から求める（Object3d の Update より前に使う）
  Matrix4x4 ComputeWorldMatrix() const;

  std::unique_ptr<SkinnedObject> skinned_;
  DragonWingAnimator wing_;
  int32_t leftTipJoint_ = -1;  // 左の翼端の指の最後の骨
  int32_t rightTipJoint_ = -1; // 右の翼端の指の最後の骨
  int32_t headJoint_ = -1;     // 頭の骨
};
