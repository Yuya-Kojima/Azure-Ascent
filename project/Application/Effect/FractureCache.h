#pragma once
#include "Effect/FractureDebris.h" // 調整値 FractureDebris::Params（入れ子の型）をメンバに持つため
#include <cstdint>
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

  /// <summary>
  /// 撃破時の爆発の調整値
  /// </summary>
  const FractureDebris::Params &GetDebrisParams() const { return debrisParams_; }

  /// <summary>
  /// 調整値の編集 UI（エディタの Inspector から呼ぶ）
  /// </summary>
  void DrawEditorUI();

private: // メンバ関数
  // 最初に使われたとき（ロード時の Prebuild より前）に、調整値を JSON から読む
  FractureCache();
  // unique_ptr<FracturePieceSet> を前方宣言のまま持つため、デストラクタは .cpp で定義する
  ~FractureCache();
  FractureCache(const FractureCache &) = delete;
  FractureCache &operator=(const FractureCache &) = delete;

  /// <summary>
  /// 調整値を JSON から読む（ファイルがなければ初期値のまま）
  /// </summary>
  void LoadConfig();

  /// <summary>
  /// 調整値を JSON に保存する
  /// </summary>
  void SaveConfig();

private: // メンバ変数
  // モデル → そのモデルを割った形セット
  std::unordered_map<const Model *, std::unique_ptr<FracturePieceSet>> sets_;

  // 撃破時の爆発の調整値
  FractureDebris::Params debrisParams_;

  // 種の数（≒ 1体あたりの破片の数）
  // 形セットを作るときにだけ使うので、変えても反映されるのは次の起動から
  int seedCount_ = 0;

  // 調整値を変えたが、まだ保存していないか
  bool isConfigDirty_ = false;
};
