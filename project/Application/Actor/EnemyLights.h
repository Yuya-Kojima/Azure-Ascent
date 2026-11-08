#pragma once
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include <memory>
#include <string>
#include <vector>

class Object3d;
class Object3dRenderer;

/// <summary>
/// 敵の体に付いて動く光る部品（目・推進器など）
/// 部品のモデルは体と同じ座標で作ってあるので、体の Object3d を親にして、体と同じ行列で描く
/// ライティングなしで、色をそのまま出す
/// </summary>
class EnemyLights {

public: // 型
  // 部品1つ分の設定（プレハブから読む）
  struct Part {
    std::string modelPath;               // 部品のモデル
    Vector3 color = {1.0f, 1.0f, 1.0f};  // 光の色
    float intensity = 1.0f;              // 色に掛ける強さ（1 を超えるとブルームでにじむ）
    std::vector<Vector3> burstPoints;    // 撃破の光を出す場所（モデルの座標）
  };

public: // メンバ関数
  // unique_ptr<Object3d> を前方宣言のまま持つため、コンストラクタとデストラクタは .cpp で定義する
  EnemyLights();
  ~EnemyLights();

  /// <summary>
  /// 部品を作る
  /// </summary>
  /// <param name="renderer">部品の Object3d を描くレンダラー</param>
  /// <param name="body">親にする体（この部品より長生きすること）</param>
  /// <param name="parts">部品の設定</param>
  void Initialize(Object3dRenderer *renderer, const Object3d *body,
                  const std::vector<Part> &parts);

  /// <summary>
  /// 行列の更新（体の Update の後に呼ぶ）
  /// </summary>
  void Update();

  /// <summary>
  /// 描画
  /// </summary>
  void Draw();

  /// <summary>
  /// すべての部品の光の色と強さを変える
  /// </summary>
  void SetColor(const Vector3 &color, float intensity);

  /// <summary>
  /// 撃破の光を出す場所（ワールド座標）。体の今の行列で、モデルの座標から変換する
  /// </summary>
  std::vector<Vector3> GetBurstPositions() const;

  /// <summary>
  /// 撃破の光の色（最初の部品の光の色。強さは掛けない）
  /// </summary>
  Vector4 GetBurstColor() const;

private: // 型
  // 部品1つ分（設定と、表示用のオブジェクトの組）
  struct Instance {
    Part part;
    std::unique_ptr<Object3d> object = nullptr;
  };

private: // メンバ変数
  // 部品
  std::vector<Instance> instances_;

  // 親にする体
  const Object3d *body_ = nullptr;
};
