#pragma once
#include "Math/Vector3.h"
#include "Render/Model/Model.h" // 引数で Model::ModelData（入れ子の型）を使うため、前方宣言では足りない
#include <cstdint>
#include <memory>
#include <vector>

/// <summary>
/// 事前に割った破片の形セット
/// ボロノイ分割は重いので、ロード時に1回だけ作り、撃破のたびに使い回す
/// （Model と Object3d の関係と同じで、ここは「形」だけを持ち、置き場所は持たない）
/// </summary>
class FracturePieceSet {

public: // 型
  // 破片1つ分の形
  struct Piece {
    // GPU 用のモデル（原点を重心に移し済みなので、置いた場所でそのまま回る）
    std::unique_ptr<Model> model = nullptr;
    // 元のモデルの座標での重心（破片を置く位置）
    Vector3 centroid = {0.0f, 0.0f, 0.0f};
    // 元のモデルの重心 → 破片の重心 の向き（長さ1。飛ばす向き）
    Vector3 direction = {0.0f, 0.0f, 0.0f};
  };

public: // メンバ関数
  /// <summary>
  /// 元のモデルをボロノイ分割して、破片の形を作る
  /// 作り直すと、前の破片の GPU バッファは解放される
  /// </summary>
  /// <param name="source">元のモデル（凸で閉じていること）</param>
  /// <param name="seedCount">種の数（≒ 破片の数）</param>
  /// <param name="randomSeed">種を置く乱数のシード（同じなら同じ割れ方）</param>
  void Create(const Model::ModelData &source, uint32_t seedCount,
              uint32_t randomSeed);

  /// <summary>
  /// 破片の一覧（使う側は読むだけ）
  /// </summary>
  const std::vector<Piece> &GetPieces() const { return pieces_; }

private: // メンバ変数
  // 破片の形の一覧
  std::vector<Piece> pieces_;
};
