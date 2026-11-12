#pragma once
#include "BaseActor.h"
#include <list>
#include <memory>
#include <vector>
#include <string>
#include <functional>

/// <summary>
/// すべてのActorを一括管理するマネージャークラス（シングルトン）
/// </summary>
class ActorManager {
public:
    static ActorManager* GetInstance();

    void Initialize();
    void Update();
    void Draw3D();
    void Draw2D();
    void Finalize();

    /// <summary>
    /// 新しいActorをリストに追加する
    /// </summary>
    /// <param name="actor">追加するActor（std::make_uniqueで渡す）</param>
    void AddActor(std::unique_ptr<BaseActor> actor);

    /// <summary>
    /// 登録されている全てのActorを削除する（シーン切り替え時などに呼ぶ）
    /// </summary>
    void Clear();

    /// <summary>
    /// 条件に一致するActorを削除フラグ(Destroy)を立てる
    /// </summary>
    void ClearActorsIf(std::function<bool(BaseActor*)> condition);

    /// <summary>
    /// 指定したタグを持つ最初のActorを取得する
    /// </summary>
    BaseActor* FindActorWithTag(ActorTag tag);

    /// <summary>
    /// 指定したタグを持つすべてのActorを取得する
    /// </summary>
    std::vector<BaseActor*> FindActorsWithTag(ActorTag tag);

    /// <summary>
    /// 条件に一致する、生きているすべてのActorを取得する
    /// </summary>
    std::vector<BaseActor*> FindActorsIf(const std::function<bool(BaseActor*)>& condition);

private:
    ActorManager() = default;
    ~ActorManager() = default;
    ActorManager(const ActorManager&) = delete;
    ActorManager& operator=(const ActorManager&) = delete;

    // Actorを管理するためのリスト
    std::list<std::unique_ptr<BaseActor>> actors_;
};
