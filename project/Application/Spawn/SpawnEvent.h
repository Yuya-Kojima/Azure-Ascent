#pragma once
#include "Actor/Enemy.h"
#include "Actor/MechSerpent.h"
#include "Math/Vector3.h"
#include <string>

/// <summary>
/// 敵の出現の 1 件（レールの進み具合が spawnTime になったら、prefabName の出し方で出す）
/// </summary>
struct SpawnEvent {
  float spawnTime = 0.0f;
  std::string prefabName = "ZakoEnemy";
  Vector3 spawnOffset = {0.0f, 0.0f, 50.0f}; // カメラからの相対位置、またはワールド絶対座標
  bool isWorldSpace = false;        // trueならspawnOffsetをワールド絶対座標として扱う
  std::string splineName = "";     // 使用するレール名（空なら直線移動）
  float splineDuration = 5.0f;     // レールを走り切る秒数
  bool isWorldSpaceSpline = false; // ワールド空間か、カメラローカル空間か
  int fireInterval = -1;           // -1なら撃たない。0以上なら指定フレーム間隔で射撃
  bool hasSpawned = false;         // 実行管理用フラグ
  MoveType moveType = MoveType::Straight;
  Vector3 moveDirection = {0.0f, 0.0f, 1.0f}; // カメラ基準の移動方向ベクトル（デフォルトは前進）
  SerpentSpec serpent;             // 大蛇（MechSerpent）の出番の決まり。ほかの出し方では使わない
};
