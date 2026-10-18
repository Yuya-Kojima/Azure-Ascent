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
