#pragma once
#include "Core/EngineBase.h"
#include "Scene/BaseScene.h"
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

private: // メンバ変数
  // 観察用カメラ（右ドラッグで回転、WASDで移動）
  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // 切断前の元メッシュ（テクスチャを差し替えたこのシーン専用のコピー）
  std::unique_ptr<Model> sourceModel_ = nullptr;

  // 切断前の元メッシュの表示用（比較用）
  std::unique_ptr<Object3d> sourceObject_ = nullptr;

private:
  /*ポインタ参照
  ------------------*/
  // エンジン
  EngineBase *engine_ = nullptr;
};
