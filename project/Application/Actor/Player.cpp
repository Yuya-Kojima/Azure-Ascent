#include "Actor/Player.h"
#include "Audio/SoundManager.h"
#include "../../externals/nlohmann/json.hpp"
#include "Actor/Enemy.h"
#include "Actor/HomingBullet.h"
#include "Actor/LockOn.h"
#include "Actor/NormalBullet.h"
#include "Camera/ICamera.h"
#include "Camera/RailCamera.h"
#include "Collision/CollisionConfig.h"
#include "Collision/CollisionManager.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Framework/ActorManager.h"
#include "Input/Input.h"
#include "Math/Geometry.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Renderer/PostProcess.h"
#include "Scene/SceneManager.h"
#include "Sprite/Sprite.h"
#include <Windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

Player::Player(const ICamera *camera) : camera_(camera) { assert(camera_); }
Player::~Player() {
  if (isAliveToken_) {
    *isAliveToken_ = false;
  }
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void Player::Initialize() {
  // ステータスのリセット（エディタからのプレイモード再開時のため）
  hp_ = 10;
  invincibleTimer_ = 0;
  isDead_ = false;
  flashIntensity_ = 0.0f;
  flashColor_ = {1.0f, 0.0f, 0.0f};

  // 自機モデルのスケール設定
  transform_.scale = {5.0f, 5.0f, 5.0f};

  // 3D視線トンネル照準の生成と初期化
  if (spriteRenderer_) {
    reticleTunnel_ = std::make_unique<ReticleTunnel>();
    reticleTunnel_->Initialize(spriteRenderer_);
  }

  // ロックオン機能の生成と初期化
  if (spriteRenderer_) {
    lockOn_ = std::make_unique<LockOn>();
    lockOn_->Initialize(spriteRenderer_);
  }

  // コライダーの初期化
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(0.4f); // プレイヤーの当たり判定の大きさ(少し小さめ)
  collider_->SetAttribute(kCollisionAttributePlayer); // 自分は「自機」
  collider_->SetMask(kCollisionAttributeEnemy |
                     kCollisionAttributeEnemyBullet); // 敵や敵の弾と当たる
  CollisionManager::GetInstance()->Register(collider_.get());

  // アクション設定の読み込み
  if (loadConfigOnInitialize_) {
    LoadActionConfig();
  }
  loadConfigOnInitialize_ = true; // 次回のためにリセットしておく
}

void Player::Update() {
  // 画面フラッシュの減衰とポストプロセスへの設定
  // (死亡時もフェードアウトを継続させるため)
  flashIntensity_ *= 0.90f;
  auto pp = SceneManager::GetInstance()->GetCurrentScenePostProcess();
  if (pp) {
    pp->SetFlashColor(flashColor_.x, flashColor_.y, flashColor_.z);
    pp->SetFlashIntensity(flashIntensity_);
  }

  if (isDead_) {
    return;
  }

  // 無敵タイマーの更新と当たり判定の切り替え
  if (invincibleTimer_ > 0) {
    invincibleTimer_--;
  }

  // プレイヤー自身の更新処理

  // 照準の移動操作と3Dパース更新
  if (reticleTunnel_) {
    reticleTunnel_->GetConfig().reticleMaxSpeed = actionConfig_.reticleMaxSpeed;
    reticleTunnel_->Update(input_, camera_, transform_.translate, IsLockOnMode());
  }

  // 自機の追従と姿勢制御
  // レティクルのNDCを計算
  float ndcX = 0.0f;
  float ndcY = 0.0f;
  if (reticleTunnel_) {
    ndcX = (reticleTunnel_->Get2DPosition().x / 1280.0f) * 2.0f - 1.0f;
    ndcY = 1.0f - (reticleTunnel_->Get2DPosition().y / 720.0f) * 2.0f;
  }

  // カメラ空間のベクトルを取得
  Matrix4x4 viewMatrix = camera_->GetViewMatrix();
  Matrix4x4 cameraWorld = Inverse(viewMatrix);
  Vector3 cameraPos = {cameraWorld.m[3][0], cameraWorld.m[3][1],
                       cameraWorld.m[3][2]};
  Vector3 cameraRight = {cameraWorld.m[0][0], cameraWorld.m[0][1],
                         cameraWorld.m[0][2]};
  Vector3 cameraUp = {cameraWorld.m[1][0], cameraWorld.m[1][1],
                      cameraWorld.m[1][2]};
  Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                           cameraWorld.m[2][2]};

  //  自機の目標座標を計算 (カメラから一定距離前方に配置)
  // カメラからの距離
  float distance = 10.0f;

  // デッドゾーンとマッピング（照準が中心付近の時は自機は移動しない）
  float deadzone = actionConfig_.playerDeadzone;
  float moveRatioX = 0.0f;
  if (std::abs(ndcX) > deadzone) {
    moveRatioX = (std::abs(ndcX) - deadzone) / (1.0f - deadzone) *
                 (ndcX > 0.0f ? 1.0f : -1.0f);
  }
  float moveRatioY = 0.0f;
  if (std::abs(ndcY) > deadzone) {
    moveRatioY = (std::abs(ndcY) - deadzone) / (1.0f - deadzone) *
                 (ndcY > 0.0f ? 1.0f : -1.0f);
  }

  // 上下方向の移動量を非対称に計算（上が広く、下が狭い）
  float moveY = 0.0f;
  if (moveRatioY >= 0.0f) {
    moveY = moveRatioY * actionConfig_.playerMaxMoveY;
  } else {
    moveY = moveRatioY * (actionConfig_.playerMaxMoveY *
                          0.5f); // 下方向には画面から消えないように制限
  }

  Vector3 targetPos =
      cameraPos +
      Vector3{cameraForward.x * distance, cameraForward.y * distance,
              cameraForward.z * distance} +
      Vector3{cameraRight.x * (moveRatioX * actionConfig_.playerMaxMoveX),
              cameraRight.y * (moveRatioX * actionConfig_.playerMaxMoveX),
              cameraRight.z * (moveRatioX * actionConfig_.playerMaxMoveX)} +
      Vector3{cameraUp.x * (moveY + actionConfig_.playerBaseOffsetY),
              cameraUp.y * (moveY + actionConfig_.playerBaseOffsetY),
              cameraUp.z * (moveY + actionConfig_.playerBaseOffsetY)};

  // 速度計算のため前回の座標を保存
  Vector3 prevPos = transform_.translate;

  // --- 奥行き（Z）の遅れを防ぐため、カメラローカル空間でXYのみLerpする ---
  // 現在の自機のワールド座標をカメラから見たローカル座標に変換
  Vector3 currentDiff = {transform_.translate.x - cameraPos.x,
                         transform_.translate.y - cameraPos.y,
                         transform_.translate.z - cameraPos.z};
  float currentLocalX = Dot(currentDiff, cameraRight);
  float currentLocalY = Dot(currentDiff, cameraUp);

  // 目標のローカル座標
  float targetLocalX = moveRatioX * actionConfig_.playerMaxMoveX;
  float targetLocalY = moveY + actionConfig_.playerBaseOffsetY;

  // ローカルX, YのみをLerp（Zは常にdistanceで固定）
  float nextLocalX =
      Lerp(currentLocalX, targetLocalX, actionConfig_.playerFollowSpeed);
  float nextLocalY =
      Lerp(currentLocalY, targetLocalY, actionConfig_.playerFollowSpeed);
  float nextLocalZ = distance;

  // ワールド座標に戻す
  Vector3 nextPos =
      cameraPos +
      Vector3{cameraRight.x * nextLocalX, cameraRight.y * nextLocalX,
              cameraRight.z * nextLocalX} +
      Vector3{cameraUp.x * nextLocalY, cameraUp.y * nextLocalY,
              cameraUp.z * nextLocalY} +
      Vector3{cameraForward.x * nextLocalZ, cameraForward.y * nextLocalZ,
              cameraForward.z * nextLocalZ};

  // カメラのワープ時はLerpせずに瞬時にスナップする
  Vector3 warpDiff = {targetPos.x - transform_.translate.x,
                      targetPos.y - transform_.translate.y,
                      targetPos.z - transform_.translate.z};
  float distSq = warpDiff.x * warpDiff.x + warpDiff.y * warpDiff.y +
                 warpDiff.z * warpDiff.z;

  if (distSq > 100.0f) { // 距離が10以上の場合はワープと判定
    transform_.translate = targetPos;
  } else {
    transform_.translate = nextPos;
  }

  //  弾の実際の目標地点を計算し、自機をそちらに向かせる
  float bulletTargetDist = 1000.0f;
  float bulletMaxMoveY =
      bulletTargetDist * std::tan(45.0f * 3.14159265f / 180.0f / 2.0f);
  float bulletMaxMoveX = bulletMaxMoveY * (1280.0f / 720.0f);
  Vector3 bulletTargetPos = cameraPos +
                            Vector3{cameraForward.x * bulletTargetDist,
                                    cameraForward.y * bulletTargetDist,
                                    cameraForward.z * bulletTargetDist} +
                            Vector3{cameraRight.x * (ndcX * bulletMaxMoveX),
                                    cameraRight.y * (ndcX * bulletMaxMoveX),
                                    cameraRight.z * (ndcX * bulletMaxMoveX)} +
                            Vector3{cameraUp.x * (ndcY * bulletMaxMoveY),
                                    cameraUp.y * (ndcY * bulletMaxMoveY),
                                    cameraUp.z * (ndcY * bulletMaxMoveY)};

  // 姿勢制御
  Vector3 velocity = {transform_.translate.x - prevPos.x,
                      transform_.translate.y - prevPos.y,
                      transform_.translate.z - prevPos.z};

  // 速度をカメラローカルに変換してバンク・ピッチを計算
  float localVelX = Dot(velocity, cameraRight);
  float localVelY = Dot(velocity, cameraUp);

  float camRotY = std::atan2(cameraForward.x, cameraForward.z);
  float xzLen = std::sqrt(cameraForward.x * cameraForward.x +
                          cameraForward.z * cameraForward.z);
  float camRotX = std::atan2(-cameraForward.y, xzLen);

  //  自機から弾の目標地点へのベクトル
  Vector3 toTarget = {bulletTargetPos.x - transform_.translate.x,
                      bulletTargetPos.y - transform_.translate.y,
                      bulletTargetPos.z - transform_.translate.z};
  float aimYaw = std::atan2(toTarget.x, toTarget.z);
  float xzLenToTarget =
      std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
  float aimPitch = std::atan2(-toTarget.y, xzLenToTarget);

  // 速度に応じて機体を傾ける
  // ピッチとヨーの基本姿勢を、カメラ正面ではなく照準の方向にする
  // 移動量に応じて機体をダイナミックに傾ける
  float targetRoll = -localVelX * actionConfig_.rollStrength;
  float targetPitch = aimPitch + localVelY * actionConfig_.pitchStrength;
  float targetYaw = aimYaw + localVelX * actionConfig_.yawStrength;

  // Lerpで滑らかに
  transform_.rotate.z =
      Lerp(transform_.rotate.z, targetRoll, actionConfig_.rollLerp);
  transform_.rotate.x =
      Lerp(transform_.rotate.x, targetPitch, actionConfig_.pitchLerp);
  transform_.rotate.y =
      Lerp(transform_.rotate.y, targetYaw, actionConfig_.yawLerp);

  UpdateTransform();

  // 射撃入力とステート管理
  if (input_) {
    // スペースキーまたはパッドのRBを射撃/ロックオンボタンとする
    bool isTrigger =
        input_->IsTriggerKey(DIK_SPACE) || input_->IsPadTrigger(PadButton::RB);
    bool isPress =
        input_->IsPressKey(DIK_SPACE) || input_->IsPadPress(PadButton::RB);
    bool isRelease =
        input_->IsReleaseKey(DIK_SPACE) || input_->IsPadRelease(PadButton::RB);

    float deltaTime = 1.0f / 60.0f; // 固定フレームレート前提

    if (isTrigger) {
      attackState_ = AttackState::Pressing;
      pressTimer_ = 0.0f;
    }

    if (attackState_ == AttackState::Pressing) {
      if (isPress) {
        pressTimer_ += deltaTime;
        // 長押しでロックオンモードへ移行
        if (pressTimer_ >= 0.15f) {
          attackState_ = AttackState::LockOn;
        }
      }
    }

    if (isRelease) {
      if (attackState_ == AttackState::Pressing) {
        // 短押し：通常ショット発射
        FireNormalShot();
        if (lockOn_) {
          lockOn_->ClearLocking(); // 短押し時は準備中のロックオン枠のみ解除
        }
      } else if (attackState_ == AttackState::LockOn) {
        // ロックオンモード終了：ホーミング弾を一斉発射
        // （FireHomingShot 内で OnHomingFired() が呼ばれ、追尾中マーカーへ昇格）
        FireHomingShot();
      }
      attackState_ = AttackState::Idle;
    }
  }

  // ホーミング弾のシーケンシャル発射キュー更新
  UpdateHomingQueue();

  // ロックオンの更新処理
  if (lockOn_) {
    bool isLockOnMode = (attackState_ == AttackState::LockOn);
    Vector2 rPos = reticleTunnel_ ? reticleTunnel_->Get2DPosition() : Vector2{640.0f, 360.0f};
    lockOn_->Update(lockOnTargets_, camera_->GetViewProjectionMatrix(),
                    rPos, isLockOnMode, actionConfig_.lockOnRadius);
  }

  // 連続衝突判定用に速度を計算してコライダーに渡す
  if (collider_) {
    collider_->SetVelocity(CalculateVelocityForCollision());
  }
}

bool Player::IsLockOnMode() const {
  return attackState_ == AttackState::LockOn;
}

void Player::FireHomingShot() {
  if (!lockOn_ || !object3dRenderer_ || !object3d_)
    return;

  const auto &targets = lockOn_->GetTargets();
  if (targets.empty())
    return;

  // 発射キューにロックオン対象をすべて登録
  homingQueue_.clear();
  homingQueue_.reserve(targets.size());
  for (size_t i = 0; i < targets.size(); ++i) {
    homingQueue_.push_back({targets[i], static_cast<int>(i), static_cast<int>(targets.size())});
  }

  // 捕捉中の敵をすべて追尾中（Tracking）マーカーへ昇格（targets_がクリアされる）
  lockOn_->OnHomingFired();

  // 1発目を即座に発射
  homingFireTimer_ = 0;
  UpdateHomingQueue();
}

void Player::UpdateHomingQueue() {
  if (homingQueue_.empty())
    return;

  if (homingFireTimer_ > 0) {
    --homingFireTimer_;
    return;
  }

  // キューの先頭を取り出して発射
  HomingQueueItem item = homingQueue_.front();
  homingQueue_.erase(homingQueue_.begin());
  homingFireTimer_ = kHomingFireInterval; // 次の弾まで数フレーム待機

  // 発射待ちの間に敵が撃破されていた場合は追尾対象を解除して直進弾として射出
  BaseActor *validTarget = item.target;
  if (validTarget && validTarget->IsDead()) {
    validTarget = nullptr;
  }

  SpawnHomingBullet(validTarget, item.index, item.totalCount);
}

void Player::SpawnHomingBullet(BaseActor *target, int index, int totalCount) {
  if (!object3dRenderer_ || !object3d_ || !camera_)
    return;

  // カメラ空間の各基底ベクトル（正面、右、上）を取得
  Matrix4x4 viewMatrix = camera_->GetViewMatrix();
  Matrix4x4 cameraWorld = Inverse(viewMatrix);
  Vector3 cameraRight = {cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2]};
  Vector3 cameraUp = {cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2]};
  Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]};

  // 自機砲門の位置
  Vector3 playerPos = object3d_->GetTranslation();
  Vector3 muzzlePos = {playerPos.x + cameraForward.x * 2.0f,
                       playerPos.y + cameraForward.y * 2.0f,
                       playerPos.z + cameraForward.z * 2.0f};

  // 弾数に応じた放射状（扇形）の拡散角度を計算
  float angle = 0.0f;
  if (totalCount > 1) {
    float progress = static_cast<float>(index) / static_cast<float>(totalCount - 1); // 0.0 ～ 1.0
    angle = (-1.0f + progress * 2.0f) * 1.05f; // 約 -60度 ～ +60度
  }

  // 放射状の拡散量
  float spreadRadius = actionConfig_.homingSpreadX * 2.2f;
  float spreadX = std::sin(angle) * spreadRadius;
  float heightOffset = ((index % 2 == 0) ? 0.4f : -0.2f) * (totalCount > 2 ? 1.0f : 0.0f);
  float spreadY = actionConfig_.homingSpeedY + heightOffset;

  // 自機の正面・右・上を基準に初速ベクトルを合成
  Vector3 initialVelocity = {
      cameraForward.x * actionConfig_.homingSpeedZ + cameraRight.x * spreadX + cameraUp.x * spreadY,
      cameraForward.y * actionConfig_.homingSpeedZ + cameraRight.y * spreadX + cameraUp.y * spreadY,
      cameraForward.z * actionConfig_.homingSpeedZ + cameraRight.z * spreadX + cameraUp.z * spreadY
  };

  Vector3 dir = Normalize(initialVelocity);
  Vector3 startPos = {muzzlePos.x + dir.x * 8.0f, muzzlePos.y + dir.y * 8.0f, muzzlePos.z + dir.z * 8.0f};

  auto bullet = std::make_unique<HomingBullet>();
  bullet->Initialize(object3dRenderer_, startPos, target, initialVelocity);

  // 3フレーム間隔でのシーケンシャル発射により確実な時間差が生まれる
  // 後ろの弾ほど誘導開始ディレイを少し短縮し、一定テンポで突き刺さるようにする
  float speed = actionConfig_.homingSpeed;
  int fallTime = (std::max)(6, actionConfig_.homingFallTime - index * 2);
  bullet->SetHomingParams(speed, fallTime, actionConfig_.homingStrengthIncrease, actionConfig_.homingStrengthMax);

  // 弾消滅時（着弾または寿命切れ）にロックオン追尾マーカーを解除するコールバックを設定
  LockOn *lockOnPtr = lockOn_.get();
  std::weak_ptr<bool> isAliveWeak = isAliveToken_;
  bullet->SetOnDestroyCallback([lockOnPtr, isAliveWeak](BaseActor *t) {
    if (!isAliveWeak.expired() && lockOnPtr) {
      lockOnPtr->RemoveTrackingTarget(t);
    }
  });

  ActorManager::GetInstance()->AddActor(std::move(bullet));

  // 発射音を小刻みに再生
  SoundManager::GetInstance()->PlaySE("lockon_fire");
}

void Player::FireNormalShot() {
  if (!object3dRenderer_ || !object3d_)
    return;

  // プレイヤーのワールド行列を取得
  Matrix4x4 playerWorld = object3d_->GetWorldMatrix();

  // 口元のローカル座標（X, Y, Z）を作成
  Vector3 localMuzzle = {actionConfig_.muzzleOffsetX, actionConfig_.muzzleOffsetY,
                         actionConfig_.muzzleOffsetForward};

  // ワールド行列を使って口元のワールド座標を算出（スケール・回転を自動適用）
  Vector3 startPos = TransformPoint(localMuzzle, playerWorld);

  // レティクルのNDC座標を計算 (-1.0 ~ 1.0)
  float ndcX = 0.0f;
  float ndcY = 0.0f;
  if (reticleTunnel_) {
    ndcX = (reticleTunnel_->Get2DPosition().x / 1280.0f) * 2.0f - 1.0f;
    ndcY = 1.0f - (reticleTunnel_->Get2DPosition().y / 720.0f) * 2.0f;
  }

  // カメラ空間のベクトルを取得
  Matrix4x4 viewMatrix = camera_->GetViewMatrix();
  Matrix4x4 cameraWorld = Inverse(viewMatrix);
  Vector3 cameraPos = {cameraWorld.m[3][0], cameraWorld.m[3][1],
                       cameraWorld.m[3][2]};
  Vector3 cameraRight = {cameraWorld.m[0][0], cameraWorld.m[0][1],
                         cameraWorld.m[0][2]};
  Vector3 cameraUp = {cameraWorld.m[1][0], cameraWorld.m[1][1],
                      cameraWorld.m[1][2]};
  Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                           cameraWorld.m[2][2]};

  // 照準の奥深くを目標座標とすることで、交差して弾が外れるのを防ぐ
  float distance = 1000.0f;
  float maxMoveY = distance * std::tan(45.0f * 3.14159265f / 180.0f / 2.0f);
  float maxMoveX = maxMoveY * (1280.0f / 720.0f);

  // 弾の目標地点は「自機から」ではなく「カメラから」見たレティクルの延長線上で計算する
  Vector3 targetPos =
      cameraPos +
      Vector3{cameraForward.x * distance, cameraForward.y * distance,
              cameraForward.z * distance} +
      Vector3{cameraRight.x * (ndcX * maxMoveX),
              cameraRight.y * (ndcX * maxMoveX),
              cameraRight.z * (ndcX * maxMoveX)} +
      Vector3{cameraUp.x * (ndcY * maxMoveY), cameraUp.y * (ndcY * maxMoveY),
              cameraUp.z * (ndcY * maxMoveY)};

  // 弾から最終目標地点へのベクトルを弾の初速度ベクトルとする
  Vector3 toTarget = {targetPos.x - startPos.x, targetPos.y - startPos.y,
                      targetPos.z - startPos.z};
  Vector3 velocity = Normalize(toTarget);

  float speed = actionConfig_.normalShotSpeed; // 弾速を外部化
  velocity.x *= speed;
  velocity.y *= speed;
  velocity.z *= speed;

  auto bullet = std::make_unique<NormalBullet>();
  bullet->Initialize(object3dRenderer_, startPos, velocity);
  ActorManager::GetInstance()->AddActor(std::move(bullet));

  SoundManager::GetInstance()->PlaySE("laser_shot");
}

void Player::Draw3D() {
  // 無敵時間中の点滅
  if (invincibleTimer_ > 0 && (invincibleTimer_ / 4) % 2 == 0) {
    return;
  }

  // プレイヤーの描画処理
  if (object3d_) {
    object3d_->Draw();
  }
}

void Player::Draw2D() {
  // メイン照準の描画
  if (reticleTunnel_) {
    reticleTunnel_->Draw();
  }

  // ロックオン照準の描画
  if (lockOn_) {
    lockOn_->Draw();
  }
}

void Player::OnCollision(Collider *other) {
  // ぶつかった時にコンソール出力
  Logger::Log("========================\n");
  Logger::Log("Player hit by something!\n");
  Logger::Log("========================\n");
}

void Player::TakeDamage(int damage) {
  // 無敵時間中はダメージを受けない
  if (invincibleTimer_ > 0) {
    return;
  }

  if (hp_ > 0) {
    hp_ -= damage;
    SoundManager::GetInstance()->PlaySE("player_damage");
    flashColor_ = {1.0f, 0.0f, 0.0f}; // 被弾フラッシュ用カラー（赤）
    flashIntensity_ = 0.6f;           // フラッシュ強度設定

    // 被弾時のカメラシェイク（強さ0.5, 時間0.2秒）
    auto railCamera =
        const_cast<RailCamera *>(dynamic_cast<const RailCamera *>(camera_));
    if (railCamera) {
      railCamera->Shake(0.5f, 0.2f);
    }

    // ヒットストップを要求
    if (hitStopCallback_) {
      hitStopCallback_(5);
    }

    if (hp_ <= 0) {
      hp_ = 0;
      isDead_ = true;
      Logger::Log("Player is DEAD!\n");
    } else {
      Logger::Log("Player took damage! Current HP: " + std::to_string(hp_) +
                  "\n");

      // ダメージを受けたら60フレーム無敵になる
      invincibleTimer_ = 60;
    }
  }
}

void Player::UpdateTransform() {
  if (object3d_) {
    // カメラの正面方向を取得
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 cameraWorld = Inverse(viewMatrix);
    Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                             cameraWorld.m[2][2]};

    object3d_->SetTranslation(transform_.translate);
    object3d_->SetRotation(transform_.rotate);
    object3d_->SetScale(transform_.scale);
    object3d_->Update();
  }
}

void Player::ForceSnapToCamera() {

  float ndcX = 0.0f;
  float ndcY = 0.0f;
  if (reticleTunnel_) {
    ndcX = (reticleTunnel_->Get2DPosition().x / 1280.0f) * 2.0f - 1.0f;
    ndcY = 1.0f - (reticleTunnel_->Get2DPosition().y / 720.0f) * 2.0f;
  }

  Matrix4x4 viewMatrix = camera_->GetViewMatrix();
  Matrix4x4 cameraWorld = Inverse(viewMatrix);
  Vector3 cameraPos = {cameraWorld.m[3][0], cameraWorld.m[3][1],
                       cameraWorld.m[3][2]};
  Vector3 cameraRight = {cameraWorld.m[0][0], cameraWorld.m[0][1],
                         cameraWorld.m[0][2]};
  Vector3 cameraUp = {cameraWorld.m[1][0], cameraWorld.m[1][1],
                      cameraWorld.m[1][2]};
  Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                           cameraWorld.m[2][2]};

  float distance = 10.0f;
  float moveRatioX = 0.0f;
  if (std::abs(ndcX) > actionConfig_.playerDeadzone) {
    moveRatioX = (std::abs(ndcX) - actionConfig_.playerDeadzone) /
                 (1.0f - actionConfig_.playerDeadzone) *
                 (ndcX > 0.0f ? 1.0f : -1.0f);
  }
  float moveRatioY = 0.0f;
  if (std::abs(ndcY) > actionConfig_.playerDeadzone) {
    moveRatioY = (std::abs(ndcY) - actionConfig_.playerDeadzone) /
                 (1.0f - actionConfig_.playerDeadzone) *
                 (ndcY > 0.0f ? 1.0f : -1.0f);
  }
  float moveY = moveRatioY >= 0.0f
                    ? moveRatioY * actionConfig_.playerMaxMoveY
                    : moveRatioY * (actionConfig_.playerMaxMoveY * 0.5f);

  Vector3 targetPos =
      cameraPos +
      Vector3{cameraForward.x * distance, cameraForward.y * distance,
              cameraForward.z * distance} +
      Vector3{cameraRight.x * (moveRatioX * actionConfig_.playerMaxMoveX),
              cameraRight.y * (moveRatioX * actionConfig_.playerMaxMoveX),
              cameraRight.z * (moveRatioX * actionConfig_.playerMaxMoveX)} +
      Vector3{cameraUp.x * (moveY + actionConfig_.playerBaseOffsetY),
              cameraUp.y * (moveY + actionConfig_.playerBaseOffsetY),
              cameraUp.z * (moveY + actionConfig_.playerBaseOffsetY)};

  transform_.translate = targetPos;
  UpdateTransform();
}

void Player::SaveActionConfig() {
  nlohmann::json root;
  root["lockOnRadius"] = actionConfig_.lockOnRadius;
  root["homingSpeed"] = actionConfig_.homingSpeed;
  root["homingFallTime"] = actionConfig_.homingFallTime;
  root["homingStrengthIncrease"] = actionConfig_.homingStrengthIncrease;
  root["homingStrengthMax"] = actionConfig_.homingStrengthMax;
  root["reticleAcceleration"] = actionConfig_.reticleAcceleration;
  root["reticleFriction"] = actionConfig_.reticleFriction;
  root["reticleMaxSpeed"] = actionConfig_.reticleMaxSpeed;
  root["playerFollowSpeed"] = actionConfig_.playerFollowSpeed;
  root["playerDeadzone"] = actionConfig_.playerDeadzone;
  root["playerMaxMoveX"] = actionConfig_.playerMaxMoveX;
  root["playerMaxMoveY"] = actionConfig_.playerMaxMoveY;
  root["playerBaseOffsetY"] = actionConfig_.playerBaseOffsetY;
  root["rollStrength"] = actionConfig_.rollStrength;
  root["pitchStrength"] = actionConfig_.pitchStrength;
  root["yawStrength"] = actionConfig_.yawStrength;
  root["rollLerp"] = actionConfig_.rollLerp;
  root["pitchLerp"] = actionConfig_.pitchLerp;
  root["yawLerp"] = actionConfig_.yawLerp;
  root["homingSpreadX"] = actionConfig_.homingSpreadX;
  root["homingSpeedY"] = actionConfig_.homingSpeedY;
  root["homingSpeedZ"] = actionConfig_.homingSpeedZ;
  root["normalShotSpeed"] = actionConfig_.normalShotSpeed;
  root["muzzleOffsetX"] = actionConfig_.muzzleOffsetX;
  root["muzzleOffsetY"] = actionConfig_.muzzleOffsetY;
  root["muzzleOffsetForward"] = actionConfig_.muzzleOffsetForward;

  if (!std::filesystem::exists("resources/config")) {
    std::filesystem::create_directories("resources/config");
  }

  std::ofstream file("resources/config/PlayerActionConfig.json");
  if (file.is_open()) {
    file << std::setw(4) << root << std::endl;
    isActionConfigDirty_ = false;
  }
}

void Player::LoadActionConfig() {
  std::ifstream file("resources/config/PlayerActionConfig.json");
  if (file.is_open()) {
    nlohmann::json root;
    try {
      file >> root;
      if (root.contains("lockOnRadius"))
        actionConfig_.lockOnRadius = root["lockOnRadius"];
      if (root.contains("homingSpeed"))
        actionConfig_.homingSpeed = root["homingSpeed"];
      if (root.contains("homingFallTime"))
        actionConfig_.homingFallTime = root["homingFallTime"];
      if (root.contains("homingStrengthIncrease"))
        actionConfig_.homingStrengthIncrease = root["homingStrengthIncrease"];
      if (root.contains("homingStrengthMax"))
        actionConfig_.homingStrengthMax = root["homingStrengthMax"];
      if (root.contains("reticleAcceleration"))
        actionConfig_.reticleAcceleration = root["reticleAcceleration"];
      if (root.contains("reticleFriction"))
        actionConfig_.reticleFriction = root["reticleFriction"];
      if (root.contains("reticleMaxSpeed"))
        actionConfig_.reticleMaxSpeed = root["reticleMaxSpeed"];
      if (root.contains("playerFollowSpeed"))
        actionConfig_.playerFollowSpeed = root["playerFollowSpeed"];
      if (root.contains("playerDeadzone"))
        actionConfig_.playerDeadzone = root["playerDeadzone"];
      if (root.contains("playerMaxMoveX"))
        actionConfig_.playerMaxMoveX = root["playerMaxMoveX"];
      if (root.contains("playerMaxMoveY"))
        actionConfig_.playerMaxMoveY = root["playerMaxMoveY"];
      if (root.contains("playerBaseOffsetY"))
        actionConfig_.playerBaseOffsetY = root["playerBaseOffsetY"];
      if (root.contains("rollStrength"))
        actionConfig_.rollStrength = root["rollStrength"];
      if (root.contains("pitchStrength"))
        actionConfig_.pitchStrength = root["pitchStrength"];
      if (root.contains("yawStrength"))
        actionConfig_.yawStrength = root["yawStrength"];
      if (root.contains("rollLerp"))
        actionConfig_.rollLerp = root["rollLerp"];
      if (root.contains("pitchLerp"))
        actionConfig_.pitchLerp = root["pitchLerp"];
      if (root.contains("yawLerp"))
        actionConfig_.yawLerp = root["yawLerp"];
      if (root.contains("homingSpreadX"))
        actionConfig_.homingSpreadX = root["homingSpreadX"];
      if (root.contains("homingSpeedY"))
        actionConfig_.homingSpeedY = root["homingSpeedY"];
      if (root.contains("homingSpeedZ"))
        actionConfig_.homingSpeedZ = root["homingSpeedZ"];
      if (root.contains("normalShotSpeed"))
        actionConfig_.normalShotSpeed = root["normalShotSpeed"];
      if (root.contains("muzzleOffsetX"))
        actionConfig_.muzzleOffsetX = root["muzzleOffsetX"];
      if (root.contains("muzzleOffsetY"))
        actionConfig_.muzzleOffsetY = root["muzzleOffsetY"];
      if (root.contains("muzzleOffsetForward"))
        actionConfig_.muzzleOffsetForward = root["muzzleOffsetForward"];

      isActionConfigDirty_ = false; // ロード成功時のみ未保存フラグをリセット
    } catch (...) {
      // Parse error, keep defaults
    }
  }
}
