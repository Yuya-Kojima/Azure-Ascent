#pragma once
#include "Render/Model/Model.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Model/SkinCluster.h"
#include "Math/MathUtil.h"
#include "Model/Skeleton.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Object3dRenderer;
class SrvManager;

class SkinnedObject {
public:
    SkinnedObject() = default;
    ~SkinnedObject() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="object3dRenderer">描画システム</param>
    /// <param name="srvManager">SRVマネージャー</param>
    /// <param name="directoryPath">リソースのディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    void Initialize(Object3dRenderer* object3dRenderer, SrvManager* srvManager, const std::string& directoryPath, const std::string& filename);

    /// <summary>
    /// 更新
    /// </summary>
    /// <param name="deltaTime">フレームの経過時間（秒）</param>
    void Update(float deltaTime);

    /// <summary>
    /// 描画
    /// </summary>
    void Draw();

    /// <summary>
    /// 関節を名前で探す
    /// </summary>
    /// <returns>関節の番号。見つからなければ std::nullopt</returns>
    std::optional<int32_t> FindJoint(const std::string& name) const;

    /// <summary>
    /// 関節の元の姿勢（モデルに入っている姿勢）での、親に対する回転
    /// </summary>
    const Quaternion& GetRestRotation(int32_t jointIndex) const;

    /// <summary>
    /// 関節の親に対する回転を上書きする（アニメーションより優先し、次の Update で反映）
    /// </summary>
    void SetJointRotation(int32_t jointIndex, const Quaternion& rotate);

    /// <summary>
    /// 関節の元の姿勢での、親に対する位置
    /// </summary>
    const Vector3& GetRestTranslation(int32_t jointIndex) const;

    /// <summary>
    /// 関節の親に対する位置を上書きする（アニメーションより優先し、次の Update で反映）
    /// </summary>
    void SetJointTranslation(int32_t jointIndex, const Vector3& translate);

    /// <summary>
    /// すべての上書き（回転・位置）をやめ、アニメーション（なければ元の姿勢）に戻す
    /// </summary>
    void ClearJointOverrides();

    /// <summary>
    /// 関節のワールド行列（最後の Update の時点）。平行移動の成分が関節の根元の位置
    /// </summary>
    Matrix4x4 GetJointWorldMatrix(int32_t jointIndex) const;

    /// <summary>
    /// 関節のモデル空間の行列を、今の上書き（回転・位置）を反映して親からたどって求める。
    /// Update の前に、書き込んだ姿勢での関節の向きや位置を知るために使う
    /// </summary>
    Matrix4x4 ComputeJointModelMatrix(int32_t jointIndex) const;

    /// <summary>
    /// アニメーションが入っているか
    /// </summary>
    bool HasAnimation() const { return animation_.duration > 0.0f; }

public:
    // ゲッター
    Object3d* GetObject3d() const { return object3d_.get(); }
    const Skeleton& GetSkeleton() const { return skeleton_; }
    Model* GetModel() const { return model_.get(); }

private:
    Object3dRenderer* object3dRenderer_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<Model> model_ = nullptr;
    std::unique_ptr<Object3d> object3d_ = nullptr;

    Animation animation_;
    float animationTime_ = 0.0f;

    Skeleton skeleton_;
    SkinCluster skinCluster_;

    // 関節ごとの元の姿勢（毎フレーム、ここから姿勢を作り直す）
    std::vector<QuaternionTransform> restTransforms_;
    // 関節ごとの回転の上書き。値がない関節は上書きしない
    std::vector<std::optional<Quaternion>> rotationOverrides_;
    // 関節ごとの位置の上書き。値がない関節は上書きしない
    std::vector<std::optional<Vector3>> translationOverrides_;
};
