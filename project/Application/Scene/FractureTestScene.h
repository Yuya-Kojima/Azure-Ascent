#pragma once
#include "Core/EngineBase.h"
#include "Math/Vector3.h"
#include "Model/Model.h" // 引数で Model::ModelData（入れ子の型）を使うため、前方宣言では足りない
#include "Scene/BaseScene.h"
#include <memory>
#include <random>
#include <vector>

class Object3d;
class DebugCamera;

/// <summary>
/// メッシュ破砕の検証用シーン
/// 余計なオブジェクトを置かず、破片の形と数値だけを確認するために使う
/// </summary>
class FractureTestScene : public BaseScene {

public: // メンバ関数
  // unique_ptr<Object3d> などを前方宣言のまま持つため、デストラクタは .cpp で定義する
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
  /// 今のモードと設定で元メッシュを切り直し、破片を作り直す
  /// GPU バッファを確保し直すので、設定が変わったときだけ呼ぶ
  /// </summary>
  void RebuildFragments();

  /// <summary>
  /// 平面1枚で表側・裏側の2つに切る（ステップA）
  /// </summary>
  void BuildPlaneFragments(const Model::ModelData &source);

  /// <summary>
  /// ボロノイ分割で複数の破片に切る（ステップB）
  /// </summary>
  void BuildVoronoiFragments(const Model::ModelData &source);

  /// <summary>
  /// 破片のメッシュから GPU 用のモデルと表示用オブジェクトを作って追加する
  /// </summary>
  /// <param name="data">破片のメッシュ（空なら何もしない）</param>
  /// <param name="separationDirection">離す向き</param>
  void AddFragment(const Model::ModelData &data,
                   const Vector3 &separationDirection);

  /// <summary>
  /// 破片を飛ばし始める（爆発した瞬間の速度・角速度を決める）
  /// </summary>
  void Explode();

  /// <summary>
  /// 飛ばした破片を元の位置に戻す
  /// </summary>
  void ResetExplosion();

  /// <summary>
  /// 飛んでいる破片を1フレーム分動かす（重力・移動・回転）
  /// </summary>
  void UpdateExplosion();

  /// <summary>
  /// 切り方の操作と体積の検証表示
  /// </summary>
  /// <returns>破片を作り直す必要があれば true</returns>
  bool DrawDebugUI();

private: // 型
  // 切り方
  enum class Mode {
    kPlane,   // 平面1枚で2分割
    kVoronoi, // ボロノイ分割
  };

  // 破片1つ分（GPU 側のモデルと、その表示用オブジェクトの組）
  struct Fragment {
    std::unique_ptr<Model> model = nullptr;
    std::unique_ptr<Object3d> object = nullptr;
    // 離す向き（平面: その破片を作った平面の法線、ボロノイ: 中心から破片への向き）
    Vector3 separationDirection = {0.0f, 0.0f, 0.0f};
    // 元メッシュの座標での、破片の原点の位置（原点を重心に移したときは重心、移さないときは 0）
    Vector3 basePosition = {0.0f, 0.0f, 0.0f};
    // 検証用の体積
    float volume = 0.0f;

    // 飛ばしているときの状態（ステップC-2）
    Vector3 position = {0.0f, 0.0f, 0.0f};        // 今の位置
    Vector3 velocity = {0.0f, 0.0f, 0.0f};        // 1秒あたりに進む量
    Vector3 rotation = {0.0f, 0.0f, 0.0f};        // 今の回転角（ラジアン）
    Vector3 angularVelocity = {0.0f, 0.0f, 0.0f}; // 1秒あたりに回る量（ラジアン）
  };

private: // メンバ変数
  // 観察用カメラ（右ドラッグで回転、WASDで移動）
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // 切断前の元メッシュ（テクスチャを差し替えたこのシーン専用のコピー）
  std::unique_ptr<Model> sourceModel_ = nullptr;

  // 切断前の元メッシュの表示用（比較用）
  std::unique_ptr<Object3d> sourceObject_ = nullptr;

  // 切断前の元メッシュの体積（破片の合計と比べる）
  float sourceVolume_ = 0.0f;

  // 破片（数はモードと設定で変わる）
  std::vector<Fragment> fragments_;

  // 今の切り方
  Mode mode_ = Mode::kVoronoi;

  // 切断平面（法線と、平面が通る点）
  Vector3 planeNormal_ = {0.0f, 1.0f, 0.0f};
  Vector3 planePoint_ = {0.0f, 0.0f, 0.0f};

  // ボロノイの種の数（ImGui のスライダーで扱うため int）
  int seedCount_ = 0;

  // ボロノイの種を置く乱数のシード（同じ値なら同じ割れ方になる）
  int randomSeed_ = 0;

  // 破片を切り口から離す距離（切り口を見やすくするため）
  float separation_ = 0.0f;

  // 元メッシュも重ねて表示するか
  bool showSource_ = false;

  // 破片の原点を重心に移すか（オフにすると移す前の回り方と比べられる）
  bool centerPivot_ = true;

  // 全破片に同じ回転をかける（原点がどこにあるかを目で確かめるため）
  Vector3 fragmentRotation_ = {0.0f, 0.0f, 0.0f};

  // 破片を飛ばしている最中か
  bool isExploding_ = false;

  // 外向きに飛ばす速さ（1秒あたりの距離）
  float explosionSpeed_ = 0.0f;

  // 引き継ぐ速度（ゲームではレールの進む速度。ここでは手で入力して代わりにする）
  Vector3 inheritedVelocity_ = {0.0f, 0.0f, 0.0f};

  // 重力加速度（1秒あたりに速度が変わる量）
  Vector3 gravity_ = {0.0f, 0.0f, 0.0f};

  // 角速度の最大値（各軸を -最大〜+最大 のランダムにする）
  float maxAngularSpeed_ = 0.0f;

  // 角速度を決める乱数（シード固定で、毎回同じ回り方になる）
  std::mt19937 randomEngine_;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
