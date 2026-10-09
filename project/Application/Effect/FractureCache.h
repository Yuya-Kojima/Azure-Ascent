#pragma once
#include <memory>
#include <unordered_map>

class Model;
class FracturePieceSet;

/// <summary>
/// 事前に割った破片の形セットの保管庫
/// ロード時にモデルを割って保存し、撃破時はモデルから形セットを探すだけにする
/// 同じモデルを使う敵は同じ Model を共有しているので、モデルごとに1セットだけ作られる
/// </summary>
class FractureCache {

public: // メンバ関数
  static FractureCache *GetInstance();

  /// <summary>
  /// モデルを割って形セットを保存する（ロード時に呼ぶ。作成済みなら何もしない）
  /// </summary>
  /// <param name="model">割るモデル（凸で閉じていること）</param>
  void Prebuild(const Model *model);

  /// <summary>
  /// 保存した形セットを探す
  /// </summary>
  /// <returns>見つからなければ nullptr（呼び出し側は旧演出に戻す）</returns>
  const FracturePieceSet *Find(const Model *model) const;

  /// <summary>
  /// 形セットを全部捨てる（GPU バッファを持つので、デバイスを消す前に呼ぶこと）
  /// 形セットを借りている爆発（FractureDebris）は、先に消しておくこと
  /// </summary>
  void Clear();

private: // メンバ関数
  FractureCache() = default;
  // unique_ptr<FracturePieceSet> を前方宣言のまま持つため、デストラクタは .cpp で定義する
  ~FractureCache();
  FractureCache(const FractureCache &) = delete;
  FractureCache &operator=(const FractureCache &) = delete;

private: // メンバ変数
  // モデル → そのモデルを割った形セット
  std::unordered_map<const Model *, std::unique_ptr<FracturePieceSet>> sets_;
};
