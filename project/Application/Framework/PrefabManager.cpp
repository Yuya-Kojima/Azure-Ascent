#include "PrefabManager.h"
#include "Actor/Enemy.h"
#include "Actor/Boss.h"
#include "Actor/Behavior/BehaviorStraight.h"
#include "Render/Object3d/Object3d.h"
#include "Actor/Behavior/BehaviorSineWave.h"
#include "Actor/Behavior/BehaviorFighter.h"
#include "Actor/Behavior/BehaviorMeteor.h"
#include "Actor/Behavior/BehaviorStrafe.h"
#include "Actor/Behavior/BehaviorTurret.h"
#include <fstream>
#include <filesystem>
#include "../../externals/nlohmann/json.hpp"

namespace {
// プレハブにモデルの指定がないときに使うモデル
std::string GetDefaultModelPath(const std::string& prefabName) {
    return (prefabName == "Boss") ? "__builtin_crystal" : "resources/suzanne.obj";
}
} // namespace

PrefabManager* PrefabManager::GetInstance() {
    static PrefabManager instance;
    return &instance;
}

void PrefabManager::Initialize(Object3dRenderer* renderer) {
    object3dRenderer_ = renderer;
    // prefabs フォルダがなければ作成
    if (!std::filesystem::exists("resources/prefabs")) {
        std::filesystem::create_directories("resources/prefabs");
    }
}

bool PrefabManager::SavePrefab(const std::string& prefabName, Enemy* enemy) {
    if (!enemy) return false;

    nlohmann::json root;
    
    // 基本パラメータ
    root["tag"] = ActorTagToString(enemy->GetTag());
    if (enemy->GetModel()) {
        root["modelPath"] = enemy->GetModel()->GetModelPath();
    }
    
    // Transform情報 (スケールのみ保存。位置と回転は出現時に決まることが多いため)
    const Transform& t = enemy->GetTransform();
    root["scale"] = {t.scale.x, t.scale.y, t.scale.z};
    
    // トラッキング対象のAI・ステータスパラメータ
    root["hp"] = enemy->GetHP();
    root["speed"] = enemy->GetSpeed();
    
    Vector3 md = enemy->GetMoveDirection();
    root["moveDirection"] = {md.x, md.y, md.z};

    std::string filepath = "resources/prefabs/" + prefabName + ".prefab";
    std::ofstream file(filepath);
    if (file.is_open()) {
        file << std::setw(4) << root << std::endl;
        return true;
    }
    return false;
}

std::string PrefabManager::GetModelPath(const std::string& prefabName) const {
    std::ifstream file("resources/prefabs/" + prefabName + ".prefab");
    if (!file.is_open()) {
        return GetDefaultModelPath(prefabName);
    }
    nlohmann::json root;
    file >> root;
    if (root.contains("modelPath")) {
        return root["modelPath"];
    }
    return GetDefaultModelPath(prefabName);
}

std::unique_ptr<Enemy> PrefabManager::InstantiateEnemy(const std::string& prefabName, const Transform& transform) {
    std::string filepath = "resources/prefabs/" + prefabName + ".prefab";
    std::ifstream file(filepath);
    
    // 生成して初期化
    std::unique_ptr<Enemy> newEnemy;
    if (prefabName == "Boss") {
        newEnemy = std::make_unique<Boss>();
    } else {
        newEnemy = std::make_unique<Enemy>();
    }
    newEnemy->Initialize();

    std::string modelPath = GetDefaultModelPath(prefabName);

    if (file.is_open()) {
        nlohmann::json root;
        file >> root;
        
        // タグの復元
        if (root.contains("tag")) {
            newEnemy->SetTag(StringToActorTag(root["tag"]));
        }
        
        // モデルパスの復元
        if (root.contains("modelPath")) {
            modelPath = root["modelPath"];
        }
        
        // スケールの復元
        if (root.contains("scale")) {
            Vector3 s = {root["scale"][0], root["scale"][1], root["scale"][2]};
            newEnemy->GetTransform().scale = s;
        }

        // ステータスの復元
        if (root.contains("hp")) {
            newEnemy->SetHP(root["hp"]);
        }
        if (root.contains("speed")) {
            newEnemy->SetSpeed(root["speed"]);
        }
        if (root.contains("moveDirection")) {
            Vector3 md = {root["moveDirection"][0], root["moveDirection"][1], root["moveDirection"][2]};
            newEnemy->SetMoveDirection(md);
        }
        
        // 位置と回転は引数で渡されたものを適用
        newEnemy->GetTransform().translate = transform.translate;
        newEnemy->GetTransform().rotate = transform.rotate;
    } else {
        // プレハブが見つからない場合はデフォルトパラメータを使用
        newEnemy->GetTransform() = transform;
    }

    // モデルを持たせる
    auto dummyModel = std::make_unique<Object3d>();
    dummyModel->Initialize(object3dRenderer_);
    dummyModel->SetModel(modelPath);
    Vector4 defaultColor = (prefabName == "Boss") ? Vector4{0.3f, 0.3f, 0.45f, 1.0f} : Vector4{1.0f, 0.2f, 0.2f, 1.0f};
    dummyModel->SetColor(defaultColor);
    newEnemy->SetModel(std::move(dummyModel));
    newEnemy->SetBaseColor(defaultColor);

    return newEnemy;
}
