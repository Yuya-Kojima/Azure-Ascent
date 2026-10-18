#pragma once
#include "Math/Matrix4x4.h"
#include "Math/Quaternion.h"
#include "Math/Vector3.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class SkinnedObject;

/// <summary>
/// 骨の並び1本ぶんの揺れもの（スプリングボーン）。
/// 骨の先端をワールド空間の点として慣性で動かし、姿勢どおりの向きへばねで引き戻して、骨の回転に直す
/// </summary>
class SpringBoneChain {
public:
  /// <summary>
  /// 揺れ方の調整値
  /// </summary>
  struct Settings {
    float stiffness = 3.0f;      // 姿勢どおりの向きへ引き戻す強さ（1/秒）
    float drag = 0.15f;          // 1/60 秒あたりに失う速さの割合（0: 減らない 〜 1: 止まる）
    float gravity = 0.0f;        // 下向きに引く強さ（1/秒）
    float maxAngle = 0.61f;      // 姿勢から曲がる最大の角度（ラジアン）
    float resetDistance = 0.5f;  // 根元がこれ以上（モデルの単位）一度に動いたら置き直す
  };

  /// <summary>
  /// 初期化
  /// </summary>
  /// <param name="jointNames">根元から先へ並べた骨の名前。最後の1つは先端の位置を決めるだけで、回さない</param>
  void Initialize(SkinnedObject *skinned,
                  const std::vector<std::string> &jointNames);

  /// <summary>
  /// 揺れを進めて骨の回転に書き込む。骨には先に、このフレームの姿勢の回転を書き込んでおくこと
  /// </summary>
  /// <param name="deltaTime">経過時間（秒）。0 なら揺れを進めず、前の回転を当てる</param>
  /// <param name="worldMatrix">このフレームの、SkinnedObject のワールド行列</param>
  void Update(float deltaTime, const Matrix4x4 &worldMatrix,
              const Settings &settings);

  /// <summary>
  /// 次の Update で、先端を姿勢どおりの位置に置き直す（揺れを消す）
  /// </summary>
  void RequestReset() { needsReset_ = true; }

  /// <summary>
  /// 回す骨の番号
  /// </summary>
  const std::vector<int32_t> &GetJointIndices() const { return jointIndices_; }

private:
  struct Bone {
    int32_t index = -1;
    std::optional<int32_t> parent;
    Vector3 localTip{};          // 骨の空間での先端の位置（次の骨の根元）
    Vector3 tip{};               // 先端のワールド座標
    Vector3 previousTip{};       // 1フレーム前の先端のワールド座標
    Quaternion appliedRotation{0.0f, 0.0f, 0.0f, 1.0f}; // 最後に書き込んだ骨の回転（親に対する）
  };

  SkinnedObject *skinned_ = nullptr;
  std::vector<Bone> bones_;
  std::vector<int32_t> jointIndices_;
  Vector3 previousRoot_{};     // 1フレーム前の、最初の骨の根元のワールド座標
  Vector3 previousRootMove_{}; // 1フレーム前に、最初の骨の根元が動いた量（ワールド）
  bool hasPreviousRoot_ = false; // previousRoot_ が前のフレームの正しい位置か
  bool needsReset_ = true;
};
