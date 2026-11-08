#include "PrefabManager.h"
#include "Actor/Enemy.h"
#include "Actor/EnemyLights.h"
#include "Actor/Boss.h"
#include "Actor/Behavior/BehaviorStraight.h"
#include "Render/Object3d/Object3d.h"
#include "Model/ModelManager.h"
#include "Actor/Behavior/BehaviorSineWave.h"
#include "Actor/Behavior/BehaviorFighter.h"
#include "Actor/Behavior/BehaviorMeteor.h"
#include "Actor/Behavior/BehaviorStrafe.h"
#include "Actor/Behavior/BehaviorTurret.h"
#include <fstream>
#include <filesystem>
#include <vector>
#include "../../externals/nlohmann/json.hpp"

namespace {
// プレハブにモデルの指定がないときに使うモデル
std::string GetDefaultModelPath(const std::string& prefabName) {
    return (prefabName == "Boss") ? "__builtin_crystal" : "resources/suzanne.obj";
}

// プレハブに色の指定がないときの体の色
Vector4 GetDefaultColor(const std::string& prefabName) {
    return (prefabName == "Boss") ? Vector4{0.3f, 0.3f, 0.45f, 1.0f} : Vector4{1.0f, 0.2f, 0.2f, 1.0f};
}

std::string GetPrefabFilePath(const std::string& prefabName) {
    return "resources/prefabs/" + prefabName + ".prefab";
}

// プレハブの JSON を読む。ファイルがない・読めないときは false
bool LoadPrefabJson(const std::string& prefabName, nlohmann::json& root) {
    std::ifstream file(GetPrefabFilePath(prefabName));
    if (!file.is_open()) {
        return false;
    }
    try {
        file >> root;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    return true;
}

// [x, y, z] を読む
Vector3 ReadVector3(const nlohmann::json& value) {
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

// [r, g, b] または [r, g, b, a] を読む（a がなければ 1）
Vector4 ReadColor(const nlohmann::json& value) {
    float a = (value.size() >= 4) ? value[3].get<float>() : 1.0f;
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), a};
}

// 光る部品の設定を読む（"lights" の配列）
std::vector<EnemyLights::Part> ReadLightParts(const nlohmann::json& root) {
    std::vector<EnemyLights::Part> parts;
    if (!root.contains("lights") || !root["lights"].is_array()) {
        return parts;
    }
    for (const nlohmann::json& entry : root["lights"]) {
        if (!entry.contains("modelPath")) {
            continue;
        }
        EnemyLights::Part part;
        part.modelPath = entry["modelPath"].get<std::string>();
        if (entry.contains("color")) {
            part.color = ReadVector3(entry["color"]);
        }
        if (entry.contains("intensity")) {
            part.intensity = entry["intensity"].get<float>();
        }
        if (entry.contains("burstPoints")) {
            for (const nlohmann::json& point : entry["burstPoints"]) {
                part.burstPoints.push_back(ReadVector3(point));
            }
        }
        parts.push_back(part);
    }
    return parts;
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

    // 既存のプレハブがあれば読み込み、ここで書く項目だけを上書きする（ここで扱わない項目は残す）
    nlohmann::json root;
    if (!LoadPrefabJson(prefabName, root) || !root.is_object()) {
        root = nlohmann::json::object();
    }

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

    std::ofstream file(GetPrefabFilePath(prefabName));
    if (file.is_open()) {
        file << std::setw(4) << root << std::endl;
        return true;
    }
    return false;
}

std::string PrefabManager::GetModelPath(const std::string& prefabName) const {
    nlohmann::json root;
    if (!LoadPrefabJson(prefabName, root)) {
        return GetDefaultModelPath(prefabName);
    }
    if (root.contains("modelPath")) {
        return root["modelPath"];
    }
    return GetDefaultModelPath(prefabName);
}

void PrefabManager::PreloadLightModels(const std::string& prefabName) const {
    nlohmann::json root;
    if (!LoadPrefabJson(prefabName, root)) {
        return;
    }
    for (const EnemyLights::Part& part : ReadLightParts(root)) {
        ModelManager::GetInstance()->LoadModel(part.modelPath);
    }
}

std::unique_ptr<Enemy> PrefabManager::InstantiateEnemy(const std::string& prefabName, const Transform& transform) {
    nlohmann::json root;
    const bool hasPrefab = LoadPrefabJson(prefabName, root);

    // 生成して初期化
    std::unique_ptr<Enemy> newEnemy;
    if (prefabName == "Boss") {
        newEnemy = std::make_unique<Boss>();
    } else {
        newEnemy = std::make_unique<Enemy>();
    }
    newEnemy->Initialize();

    std::string modelPath = GetDefaultModelPath(prefabName);

    if (hasPrefab) {
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

    // 体の色（プレハブに色がなければ、ボスは灰青・それ以外は赤）
    Vector4 bodyColor = GetDefaultColor(prefabName);
    if (hasPrefab && root.contains("color")) {
        bodyColor = ReadColor(root["color"]);
    }

    // モデルを持たせる
    auto dummyModel = std::make_unique<Object3d>();
    dummyModel->Initialize(object3dRenderer_);
    dummyModel->SetModel(modelPath);
    dummyModel->SetColor(bodyColor);
    newEnemy->SetModel(std::move(dummyModel));
    newEnemy->SetBaseColor(bodyColor);

    if (hasPrefab) {
        // 被弾したときの色
        if (root.contains("hitFlashColor")) {
            newEnemy->SetHitFlashColor(ReadColor(root["hitFlashColor"]));
        }

        // ふちの光
        if (root.contains("rimLight")) {
            const nlohmann::json& rim = root["rimLight"];
            Vector3 rimColor = {1.0f, 1.0f, 1.0f};
            if (rim.contains("color")) {
                rimColor = ReadVector3(rim["color"]);
            }
            float intensity = rim.value("intensity", 0.0f);
            float power = rim.value("power", 3.0f);
            newEnemy->SetRimLight(rimColor, intensity, power);
        }

        // 光る部品（体のモデルを親にするので、モデルを持たせた後に作る）
        std::vector<EnemyLights::Part> parts = ReadLightParts(root);
        if (!parts.empty()) {
            auto lights = std::make_unique<EnemyLights>();
            lights->Initialize(object3dRenderer_, newEnemy->GetModel(), parts);
            newEnemy->SetLights(std::move(lights));
        }
    }

    return newEnemy;
}
