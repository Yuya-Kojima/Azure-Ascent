#pragma once
#include "Actor/Enemy.h"
#include "Actor/FloatingFortress.h"
#include "Actor/MechSerpent.h"
#include "Math/Vector3.h"
#include "Spawn/SpawnEvent.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Boss;
class InterceptCue;
class Object3dRenderer;
class Player;
class RailCamera;
class SpriteRenderer;

/// <summary>
/// 敵を出すときに使う道具（シーンが持っている物を借りる）
/// </summary>
struct SpawnContext {
  RailCamera *camera = nullptr;                 // レールカメラ
  Player *player = nullptr;                     // 自機
  Object3dRenderer *object3dRenderer = nullptr; // モデルの描画係
  SpriteRenderer *spriteRenderer = nullptr;     // スプライトの描画係
  const std::unordered_map<std::string, std::vector<Vector3>> *splines =
      nullptr;                         // 名前つきの道筋
  InterceptCue *interceptCue = nullptr; // 迎撃の印
  std::function<void(Enemy *, float)>
      countForScore; // 出た敵として数える（数える敵, 出たときのレールの進み具合）
};

/// <summary>
/// 出した物（受け取ったシーンが、それぞれの一覧に入れる）
/// </summary>
struct SpawnResult {
  std::vector<std::unique_ptr<Enemy>> enemies; // 敵の一覧に入れる物
  std::unique_ptr<FloatingFortress> fortress;  // 浮かぶ砦（砦を出したときだけ）
  std::unique_ptr<MechSerpent> serpent;        // 大蛇（大蛇を出したときだけ）
  Boss *boss = nullptr; // ボス（ボスを出したときだけ。enemies の中の物を指す）
};

/// <summary>
/// 敵の出し方の基底クラス。出現の 1 件から敵を作って返す
/// </summary>
class EnemySpawner {
public:
  virtual ~EnemySpawner() = default;

  /// <summary>
  /// 出現の 1 件 ev の敵を作って返す
  /// </summary>
  virtual SpawnResult Spawn(const SpawnEvent &ev,
                            const SpawnContext &context) = 0;

protected:
  /// <summary>
  /// 出す位置。ev.isWorldSpace ならワールドの位置 ev.spawnOffset そのまま、
  /// そうでなければ今のレールの基準の位置から、レールの右・上・前へ ev.spawnOffset だけずらした位置
  /// </summary>
  static Vector3 CalcSpawnPosition(const SpawnEvent &ev,
                                   const RailCamera &camera);
};
