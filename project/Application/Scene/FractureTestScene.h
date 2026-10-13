#pragma once
#include "Core/EngineBase.h"
#include "Effect/FractureDebris.h" // 調整値 FractureDebris::Params（入れ子の型）をメンバに持つため
#include "Effect/FracturePieceSet.h" // シーンを作る側（SceneFactory）で unique_ptr の破棄に完全な型が要るため
#include "Math/Vector3.h"
#include "Model/Model.h" // 引数で Model::ModelData（入れ子の型）を使うため、前方宣言では足りない
#include "Scene/BaseScene.h"
#include <memory>
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
  /// ゲームと同じ部品（FracturePieceSet と FractureDebris）で爆発させる
  /// </summary>
  void Explode();

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

  // ボロノイの種の偏らせ方（オン/オフで破片の大小の差を比べる）
  MeshFracture::SeedBias seedBias_;

  // 偏らせたときの衝撃点（元メッシュの座標）と、その位置に置く目印
  Vector3 impactPoint_ = {0.0f, 0.0f, 0.0f};
  std::unique_ptr<Object3d> impactMarker_ = nullptr;

  // 破片を切り口から離す距離（切り口を見やすくするため）
  float separation_ = 0.0f;

  // 元メッシュも重ねて表示するか
  bool showSource_ = false;

  // 破片の原点を重心に移すか（オフにすると移す前の回り方と比べられる）
  bool centerPivot_ = true;

  // 全破片に同じ回転をかける（原点がどこにあるかを目で確かめるため）
  Vector3 fragmentRotation_ = {0.0f, 0.0f, 0.0f};

  // 爆発に使う破片の形（ゲームではロード時に1回作るもの）
  // 爆発（debris_）が借りて使うので、debris_ より先に消してはいけない
  // メンバは宣言と逆の順に破棄されるため、debris_ より上に宣言しておく
  std::unique_ptr<FracturePieceSet> pieceSet_ = nullptr;

  // 爆発中の演出（寿命が来たら消して、止まった破片の表示に戻る）
  std::unique_ptr<FractureDebris> debris_ = nullptr;

  // 爆発の調整値
  FractureDebris::Params debrisParams_;

  // 引き継ぐ速度（ゲームではレールの進む速度。ここでは手で入力して代わりにする）
  Vector3 inheritedVelocity_ = {0.0f, 0.0f, 0.0f};

  // 爆発させる敵の大きさ（形セットを拡大して使えるかの確認用）
  float debrisScale_ = 1.0f;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
