#pragma once
#include "Audio/SoundManager.h"
#include "Core/EngineBase.h"
#include "Debug/DebugCamera.h"
#include "Math/MathUtil.h"
#include "Object3d/Object3d.h"
#include "Particle/BillboardParticleEmitter.h"
#include "Particle/MeshParticleEmitter.h"
#include "Particle/ParticleEmitter.h"
#include "Render/Object3d/SkinnedObject.h"
#include "Actor/DragonWingAnimator.h"
#include "Render/Renderer/SkyBoxRenderer.h"
#include "Render/SkyBox/SkyBox.h"
#include "Scene/BaseScene.h"
#include "Scene/LevelData.h"
#include "Sprite/Sprite.h"
#include <memory>
#include <vector>
#include "Camera/RailCamera.h"

class GameCamera;
class SpriteRenderer;
class Object3dRenderer;
class InputKeyState;
class Player;

class DebugScene : public BaseScene {

private: // メンバ変数(ゲーム用)
  // プレイヤーへのポインタ（ActorManagerで管理しつつ、ここで情報を渡すため）
  Player* playerPtr_ = nullptr;

  // カメラ
  std::unique_ptr<GameCamera> camera_ = nullptr;
  std::unique_ptr<RailCamera> railCamera_ = nullptr;

  Transform cameraTransform_{};

  std::unique_ptr<DebugCamera> debugCamera_ = nullptr;

  // デバッグカメラ切り替え
  bool useDebugCamera_ = false;
  bool isRailCameraMoving_ = true; // デバッグ用：レールカメラの進行フラグ

  // レールカメラ用ポイント
  std::vector<Vector3> waypoints_;

  static constexpr int kSpriteCount_ = 5;

  std::vector<std::unique_ptr<Sprite>> sprites_;

  std::unique_ptr<Sprite> sprite_ = nullptr;

  Vector2 spritePositions_[kSpriteCount_];

  Vector2 spriteSizes_[kSpriteCount_];

  // スプライトのTransform
  Vector2 spritePosition_{};

  float spriteRotation_{};

  Vector4 spriteColor_{};

  Vector2 spriteSize_{};

  Vector2 spriteAnchorPoint_{};

  bool isFlipX_ = false;
  bool isFlipY_ = false;

  Transform uvTransformSprite_{};

  std::unique_ptr<Object3d> object3d_ = nullptr;
  
  // テスト用ターゲットのリスト
  std::vector<std::unique_ptr<Object3d>> testTargets_;

  std::unique_ptr<Object3d> animatedCube_ = nullptr;
  Animation animation_;

  std::unique_ptr<SkinnedObject> sneakWalk_ = nullptr;
  std::unique_ptr<Model> sphereModel_ = nullptr;
  std::vector<std::unique_ptr<Object3d>> jointObjects_;

  // 自機ドラゴンの見比べ（左に OBJ、右にボーン付きの glTF を表示）
  std::unique_ptr<Object3d> dragonObj_ = nullptr;
  std::unique_ptr<SkinnedObject> dragonSkinned_ = nullptr;
  int dragonTestJoint_ = 0;             // 回して確かめる関節の番号
  Vector3 dragonTestJointAngles_ = {};  // その関節の、元の姿勢からの回転（ラジアン、関節のローカル軸）
  DragonWingAnimator dragonWing_;
  DragonWingAnimator::State dragonWingState_;
  bool dragonAnimateWing_ = true;
  // 体を揺らす確認（ロール・ピッチ・上下を sin で動かし、頭が残るかを見る）
  bool dragonShake_ = false;
  float dragonShakeTime_ = 0.0f;                 // 揺らし始めてからの時間（秒）
  float dragonShakeRoll_ = 0.5f;                 // ロールの振れ幅（ラジアン）
  float dragonShakePitch_ = 0.3f;                // ピッチの振れ幅（ラジアン）
  float dragonShakeHeight_ = 0.3f;               // 上下の振れ幅
  float dragonShakePeriod_ = 2.0f;               // ロールの1往復の時間（秒）
  static constexpr float kDragonShakePitchRate_ = 1.3f;  // ピッチの速さ（ロールに対する倍率）
  static constexpr float kDragonShakeHeightRate_ = 1.7f; // 上下の速さ（ロールに対する倍率）
  // 急に横へ動かす確認（ボタンを押すたびに左右の位置を行き来し、尾と脚が遅れるかを見る）
  float dragonSideOffset_ = 0.0f;   // 今の横のずれ
  float dragonSideTarget_ = 0.0f;   // 目指す横のずれ
  float dragonSideDistance_ = 3.0f; // 1回で動く距離
  float dragonSideSpeed_ = 12.0f;   // 目指す位置へ近づく速さ（1/秒）
  std::unique_ptr<Object3d> dragonTipMarker_ = nullptr; // 翼端の関節の位置の印
  int32_t dragonTipJoint_ = 0;
  static constexpr float kDragonDeltaTime_ = 1.0f / 60.0f; // 1フレームの時間（秒）
  bool showDragonCompare_ = true;
  Vector3 dragonCompareCenter_ = {0.0f, 3.0f, 0.0f};
  float dragonCompareYaw_ = 0.0f;     // Y 軸まわりの回転（ラジアン）
  float dragonCompareScale_ = 3.0f;   // 2体共通の拡大率
  float dragonCompareSpacing_ = 4.0f; // 2体の中心どうしの左右の間隔

  float rotateObj_{};

  float cylinderUVOffset_ = 0.0f;
  float cylinderColorHue_ = 0.0f;

  std::unique_ptr<BillboardParticleEmitter> testParticleGroup_;
  std::unique_ptr<BillboardParticleEmitter> clearParticleGroup_;
  std::unique_ptr<MeshParticleEmitter> hitParticleGroup_;
  std::unique_ptr<MeshParticleEmitter> cylinderParticleGroup_;
  std::unique_ptr<MeshParticleEmitter> planeHitParticleGroup_;

  std::unique_ptr<ParticleEmitter> particleEmitter_ = nullptr;
  std::unique_ptr<ParticleEmitter> clearParticleEmitter_ = nullptr;
  std::unique_ptr<ParticleEmitter> hitParticleEmitter_ = nullptr;
  std::unique_ptr<ParticleEmitter> cylinderEmitter_ = nullptr;
  std::unique_ptr<ParticleEmitter> planeHitParticleEmitter_ = nullptr;

  // 粒の動き（重力・風・抵抗・重さ・色と大きさの変化）を試す入れ物
  std::unique_ptr<BillboardParticleEmitter> motionTestGroup_;
  bool motionTestAutoEmit_ = true;                    // 一定の間隔でまとめて出し続ける
  float motionTestInterval_ = 1.0f;                   // まとめて出す間隔（秒）
  float motionTestTimer_ = 0.0f;                      // 前に出してからの時間（秒）
  static constexpr float kMotionTestDeltaTime_ = 1.0f / 60.0f; // 1フレームの時間（秒）
  static constexpr uint32_t kMotionTestMaxParticles_ = 300000;  // 試しの入れ物の粒の上限
  static constexpr int kMotionTestMaxCount_ = 100000;           // 1 回に出す数のつまみの上限
  int motionTestRequests_ = 1;                        // 1 フレームに積む頼みの数（出す位置を横に並べる）
  // 箱の表面から出す試し
  int motionTestShape_ = 0;                           // 0: 中心のまわり、1: 箱の表面
  Vector3 motionTestBoxCenter_ = {0.0f, 4.0f, 20.0f}; // 箱の中心
  Vector3 motionTestBoxHalfSize_ = {6.0f, 2.0f, 3.0f}; // 箱の半分の大きさ（m）
  Vector3 motionTestBoxRotate_ = {0.0f, 0.4f, 0.0f};  // 箱の回転（ラジアン）
  Vector3 motionTestBandCenter_ = {-5.5f, 4.0f, 22.0f}; // 距離を測る点（撃ち抜いた点の代わり）
  float motionTestBandWidth_ = 0.5f;                  // 出す距離の範囲の幅（m）
  float motionTestFrontSpeed_ = 10.0f;                // 前線が広がる速さ（m/秒）
  float motionTestSurfaceSpeed_ = 1.0f;               // 面の外向きにはがれる速さ（m/秒）
  bool motionTestFrontRunning_ = false;               // 前線を走らせている間
  float motionTestFrontRadius_ = 0.0f;                // 今の前線の、距離を測る点からの距離（m）
  // 前線で消える本物の箱（距離のディゾルブの試し）
  std::unique_ptr<Object3d> motionTestBoxModel_;
  float motionTestDissolveNoise_ = 0.6f;              // 境目を模様の画像でずらす幅（m）
  float motionTestDissolveEdge_ = 0.6f;               // 境目が光る幅（m）
  Vector4 motionTestDissolveEdgeColor_ = {1.0f, 0.25f, 0.1f, 1.0f}; // 境目の光の色
  static constexpr Vector4 kMotionTestBoxColor_ = {0.15f, 0.15f, 0.17f, 1.0f}; // 箱の色（暗い金属）

  /// <summary>
  /// 試しの箱の中 → ワールドの行列（回転と位置だけ）
  /// </summary>
  Matrix4x4 MotionTestBoxWorld() const;

  /// <summary>
  /// 試しの箱の 12 本の辺を線で描く
  /// </summary>
  void DrawMotionTestBox();
  bool motionTestEmitOnce_ = false;                   // 次のフレームで 1 回だけ出す
  Vector3 motionTestCenter_ = {0.0f, 4.0f, 8.0f};     // 出す位置
  int motionTestCount_ = 64;                          // 1 回に出す数
  float motionTestSpeed_ = 4.0f;                      // 出すときの速さのばらつき（m/秒）
  float motionTestLife_ = 3.0f;                       // 寿命（秒）
  float motionTestWeightMin_ = 0.2f;                  // 重さの最小
  float motionTestWeightMax_ = 4.0f;                  // 重さの最大
  Vector3 motionTestGravity_ = {0.0f, -9.8f, 0.0f};   // 重力の加速度（m/秒²）
  Vector3 motionTestWind_ = {6.0f, 0.0f, 0.0f};       // 風の速さ（m/秒）
  float motionTestDrag_ = 2.0f;                       // 空気の抵抗の強さ（1/秒）
  Vector4 motionTestColorStart_ = {1.0f, 0.25f, 0.1f, 1.0f}; // 始まりの色
  Vector4 motionTestColorEnd_ = {0.12f, 0.12f, 0.14f, 1.0f}; // 終わりの色
  float motionTestScale_ = 0.3f;                      // 始まりの大きさ
  float motionTestScaleEndRate_ = 0.2f;               // 終わりの大きさの倍率
  float motionTestFadeStart_ = 0.7f;                  // 薄くなり始める寿命の割合

  /// <summary>
  /// 試しの入れ物から粒を出す
  /// </summary>
  void EmitMotionTest();

  // スカイボックス
  std::unique_ptr<Skybox> skybox_ = nullptr;

  // レベルデータ
  std::unique_ptr<LevelData> levelData_ = nullptr;
  std::vector<std::unique_ptr<Object3d>> levelObjects_;

  // 敵リスト
  std::vector<std::unique_ptr<Object3d>> enemies_;

  // レベルデータのオブジェクト再帰生成
  void CreateObjectsRecursive(const LevelData::ObjectData &objectData,
                              Object3d *parent);

  //========================
  // ライトを格子状に並べる台（ライトの数と重さを確かめる）
  //========================
  bool lightGridEnabled_ = false;
  int lightGridPointCount_ = 16;              // 並べる点の光の数
  int lightGridSpotCount_ = 0;                // 並べるスポットライトの数
  Vector3 lightGridCenter_ = {0.0f, 1.5f, 0.0f}; // 格子の中心
  float lightGridSpacing_ = 3.0f;             // 隣のライトとの間隔
  float lightGridRadius_ = 4.0f;              // 点の光の届く距離
  float lightGridIntensity_ = 2.0f;           // 輝度
  float lightGridSpotHeight_ = 4.0f;          // スポットライトを格子の中心から上げる高さ
  float lightGridSpotAngle_ = 0.5f;           // スポットライトの円錐の半分の角度（ラジアン）

  /// <summary>
  /// 格子のライトを名簿に書く（毎フレーム呼ぶ）
  /// </summary>
  void AddLightGrid() const;

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

  void Draw3D() override;

  void Draw2D() override;

  ~DebugScene() override;

private: // メンバ変数(システム用)
  // エンジン
  EngineBase *engine_ = nullptr;

  //========================
  // Dissolveアニメーション用
  //========================
  bool suzanneEnableDissolve_ = false;
  float suzanneDissolveThreshold_ = 0.0f;
  float suzanneDissolveEdgeRange_ = 0.05f;
  Vector2 suzanneMaskTransform_ = {0.0f, 0.0f};
  Vector4 suzanneDissolveEdgeColor_ = {1.0f, 0.4f, 0.3f, 1.0f};
  bool isPlayingSuzanneDissolve_ = false;
  bool isHologramMode_ = false;

  //========================
  // PostProcess用
  //========================
  uint32_t noise0TextureIndex_ = 0;
  uint32_t noise1TextureIndex_ = 0;
  int useNoiseTextureType_ = 0;
  float elapsedTime_ = 0.0f;
};
