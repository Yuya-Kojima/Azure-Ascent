#pragma once
#include <string>
#include <memory>
#include "Math/Transform.h"

// 前方宣言
class Enemy;
class Object3dRenderer;

class PrefabManager {
public:
    static PrefabManager* GetInstance();

    void Initialize(Object3dRenderer* renderer);
    
    Object3dRenderer* GetObject3dRenderer() const { return object3dRenderer_; }

    /// <summary>
    /// 現在のエネミーのパラメータをプレハブとしてJSON保存する
    /// </summary>
    bool SavePrefab(const std::string& prefabName, Enemy* enemy);

    /// <summary>
    /// プレハブデータ（JSON）から新しいEnemyを生成する
    /// </summary>
    std::unique_ptr<Enemy> InstantiateEnemy(const std::string& prefabName, const Transform& transform);

    /// <summary>
    /// プレハブが使うモデルのパスを返す（敵を生成せずに、モデルだけ先に準備したいとき用）
    /// InstantiateEnemy が使うパスと同じものを返す
    /// </summary>
    std::string GetModelPath(const std::string& prefabName) const;

private:
    PrefabManager() = default;
    ~PrefabManager() = default;
    PrefabManager(const PrefabManager&) = delete;
    PrefabManager& operator=(const PrefabManager&) = delete;

    Object3dRenderer* object3dRenderer_ = nullptr;
};
