#pragma once
#include "Core/EngineBase.h"
#include "Math/Vector3.h"
#include "Scene/BaseScene.h"
#include <array>
#include <memory>

class Object3d;
class Model;
class DebugCamera;

/// <summary>
/// メッシュ破砕の検証用シーン
/// 余計なオブジェクトを置かず、破片の形と数値だけを確認するために使う
/// </summary>
class FractureTestScene : public BaseScene {

public: // メンバ関数
  // unique_ptr<Model> を前方宣言のまま持つため、デストラクタは .cpp で定義する
  ~FractureTestScene() override;

  /// <summary>
  /// 初期化
  /// </summary>
  void Initialize(EngineBase *engine) override;

  /// <summary>
  /// 終了
  /// </summary>
  void Finalize() override;

  /// <summary>
  /// 更新
  /// </summary>
  void Update() override;

  /// <summary>
  /// 描画
  /// </summary>
  void Draw() override;

  /// <summary>
  /// 2Dオブジェクト描画
  /// </summary>
  void Draw2D() override;

  /// <summary>
  /// 3Dオブジェクト描画
  /// </summary>
  void Draw3D() override;

private: // メンバ関数
  /// <summary>
  /// 今の切断平面で元メッシュを切り直し、破片を作り直す
  /// GPU バッファを確保し直すので、平面が変わったときだけ呼ぶ
  /// </summary>
  void RebuildFragments();

  /// <summary>
  /// 切断平面の操作と体積の検証表示
  /// </summary>
  /// <returns>平面が変わったら true</returns>
  bool DrawDebugUI();

private: // 型
  // 破片1つ分（GPU 側のモデルと、その表示用オブジェクトの組）
  struct Fragment {
    std::unique_ptr<Model> model = nullptr;
    std::unique_ptr<Object3d> object = nullptr;
    // 切り口から離す向き（この破片を作った平面の法線）
    Vector3 separationDirection = {0.0f, 0.0f, 0.0f};
    // 検証用の体積
    float volume = 0.0f;
  };

  // 平面1枚で切るので、表側と裏側の2つ
  static constexpr size_t kFragmentCount_ = 2;

private: // メンバ変数
  // 観察用カメラ（右ドラッグで回転、WASDで移動）
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // 切断前の元メッシュ（テクスチャを差し替えたこのシーン専用のコピー）
  std::unique_ptr<Model> sourceModel_ = nullptr;

  // 切断前の元メッシュの表示用（比較用）
  std::unique_ptr<Object3d> sourceObject_ = nullptr;

  // 切断前の元メッシュの体積（破片の合計と比べる）
  float sourceVolume_ = 0.0f;

  // 破片
  std::array<Fragment, kFragmentCount_> fragments_;

  // 切断平面（法線と、平面が通る点）
  Vector3 planeNormal_ = {0.0f, 1.0f, 0.0f};
  Vector3 planePoint_ = {0.0f, 0.0f, 0.0f};

  // 破片を切り口から離す距離（切り口を見やすくするため）
  float separation_ = 0.0f;

  // 元メッシュも重ねて表示するか
  bool showSource_ = false;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
