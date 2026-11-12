#include "Actor/Player.h"
#include "Audio/SoundManager.h"
#include "../../externals/nlohmann/json.hpp"
#include "Actor/DragonModel.h"
#include "Actor/Enemy.h"
#include "Actor/HomingBullet.h"
#include "Actor/LockOn.h"
#include "Actor/NormalBullet.h"
#include "Score/ScoreKeeper.h"
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
#include <limits>

namespace {
// 飛んでいる間の見た目の向きが、目標の向きへ 1 フレームで近づく割合
const float kFlightTurnRate_ = 0.12f;
// 1 フレームの秒数
const float kFrameTime_ = 1.0f / 60.0f;

/// <summary>
/// 回転（クォータニオン）を長さ 1 にする
/// </summary>
Quaternion NormalizeRotation(const Quaternion &q) {
  const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (length <= 0.0f) {
    return {0.0f, 0.0f, 0.0f, 1.0f};
  }
  return {q.x / length, q.y / length, q.z / length, q.w / length};
}
} // namespace

Player::Player(const RailCamera *camera) : camera_(camera) { assert(camera_); }
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
  useFlightOrientation_ = false;
  hasFramingOffset_ = false;
  hasPrevRailForward_ = false;
  railTurnRate_ = 0.0f;
  railClimb_ = 0.0f;

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

  // カメラの構図が前のフレームから進んだ量を記録する
  {
    const Vector3 framingPos = camera_->GetFraming().position;
    framingVelocity_ = hasPrevFramingPos_ ? framingPos - prevFramingPos_
                                          : Vector3{0.0f, 0.0f, 0.0f};
    prevFramingPos_ = framingPos;
    hasPrevFramingPos_ = true;
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

  // 自機を置く土台はカメラの構図（自機についていく首振り・バンク・揺れの前）。照準の向きと機体の姿勢は画面のカメラで決める
  const RailCamera::Framing &frame = camera_->GetFraming();

  //  自機の目標座標を計算 (構図の位置から一定距離前方に配置)
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

  // 可動域（playerMaxMoveX・Y、playerBaseOffsetY）は画面の上で見えるずれ。カメラの首振りで縮む分を足した構図の上の位置に立つ
  const Vector2 targetLocal = camera_->ToFramingOffset(
      {moveRatioX * actionConfig_.playerMaxMoveX, moveY + actionConfig_.playerBaseOffsetY});

  // 構図の中の位置（右・上）は自機が持ち、構図が動いたり振り向いたりしても同じ位置のまま運ばれる
  // 演出が位置を決めた後は、そのときの位置を構図から測って続ける
  if (!hasFramingOffset_) {
    const Vector3 currentDiff = transform_.translate - frame.position;
    framingOffset_ = {Dot(currentDiff, frame.right), Dot(currentDiff, frame.up)};
    hasFramingOffset_ = true;
    framingVelocity2D_ = {0.0f, 0.0f};
  }

  // 右・上だけを、ばねとダンパーで目標へ引き寄せる（前は常に distance）
  // 速さを持つので、照準より遅れて加速し、少し滑ってから止まる
  {
    const float omega =
        kFollowSettleFactor_ / (std::max)(actionConfig_.playerFollowTime, kFrameTime_);
    const float damping = 2.0f * actionConfig_.playerFollowDamping * omega;
    const Vector2 accel = {
        omega * omega * (targetLocal.x - framingOffset_.x) - damping * framingVelocity2D_.x,
        omega * omega * (targetLocal.y - framingOffset_.y) - damping * framingVelocity2D_.y};
    framingVelocity2D_.x += accel.x * kFrameTime_;
    framingVelocity2D_.y += accel.y * kFrameTime_;
  }

  // 自機が構図の中で動いた量（右・上、m/フレーム）。構図そのものの移動・回転は含めない
  const float localVelX = framingVelocity2D_.x * kFrameTime_;
  const float localVelY = framingVelocity2D_.y * kFrameTime_;
  framingOffset_.x += localVelX;
  framingOffset_.y += localVelY;

  // レールの曲がる速さと登りの角度を測る（構図の演出の振り向き・ずれは含めない）
  {
    const float kRadToDeg = 180.0f / std::numbers::pi_v<float>;
    const float kFramesPerSecond = 60.0f;
    const Vector3 &railForward = camera_->GetRailForward();
    float measuredTurn = 0.0f;
    if (hasPrevRailForward_) {
      // 前のフレームの向きから今の向きへ、レールの上の軸まわりに回った角度（右が正）
      const float turnSin = Dot(Cross(prevRailForward_, railForward), camera_->GetRailUp());
      measuredTurn = std::asin(std::clamp(turnSin, -1.0f, 1.0f)) * kRadToDeg * kFramesPerSecond;
      measuredTurn = std::clamp(measuredTurn, -kMaxRailTurnRate_, kMaxRailTurnRate_);
    }
    prevRailForward_ = railForward;
    hasPrevRailForward_ = true;
    const float measuredClimb = std::asin(std::clamp(railForward.y, -1.0f, 1.0f)) * kRadToDeg;
    railTurnRate_ = Lerp(railTurnRate_, measuredTurn, actionConfig_.railFollow);
    railClimb_ = Lerp(railClimb_, measuredClimb, actionConfig_.railFollow);
  }

  // カーブの外側と、登っている間は下へ、画面の上で遅れて見える位置に置く（操作の位置とは別に足す）
  const Vector2 railLag = camera_->ToFramingOffset(
      {-railTurnRate_ * actionConfig_.railLagPerTurn,
       -(std::max)(railClimb_, 0.0f) * actionConfig_.railLagPerClimb});

  transform_.translate = frame.position + frame.right * (framingOffset_.x + railLag.x) +
                         frame.up * (framingOffset_.y + railLag.y) + frame.forward * distance;

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

  // 姿勢制御（バンク・ピッチは、自機がレールの構図の中で動いた量から決める）
  float camRotY = std::atan2(cameraForward.x, cameraForward.z);
  float xzLen = std::sqrt(cameraForward.x * cameraForward.x +
                          cameraForward.z * cameraForward.z);
  float camRotX = std::atan2(-cameraForward.y, xzLen);

  //  自機から弾の目標地点へのベクトル
  Vector3 toTarget = {bulletTargetPos.x - transform_.translate.x,
                      bulletTargetPos.y - transform_.translate.y,
                      bulletTargetPos.z - transform_.translate.z};

  // 動きから羽ばたきの状態を決める（上昇で強く、下降で畳み気味、左右の移動で左右差）
  flightWingState_.strength =
      std::clamp(actionConfig_.wingStrength +
                     localVelY * actionConfig_.wingClimbStrength,
                 0.0f, 1.0f);
  flightWingState_.frequency = actionConfig_.wingFrequency;
  flightWingState_.fold =
      std::clamp(-localVelY * actionConfig_.wingDiveFold, 0.0f, 1.0f);
  flightWingState_.asymmetry =
      std::clamp(localVelX * actionConfig_.wingTurnAsymmetry, -1.0f, 1.0f);
  flightWingState_.hover = std::clamp(
      actionConfig_.wingHover + localVelY * actionConfig_.wingClimbHover, 0.0f,
      1.0f);

  // 体は進む向き（レールの前・上）、頭は見る向き（照準の方向）
  // 照準が首で向けられる角度より外にあるときは、はみ出した分だけ体も照準の方へ向ける
  const Vector3 aimForward = SafeNormalize(toTarget);
  const Vector3 &railForward = camera_->GetRailForward();
  Vector3 bodyForward = railForward;
  if (dragonModel_) {
    const float neckMaxRad =
        dragonModel_->GetWingAnimator().GetConfig().headMaxAngle * (std::numbers::pi_v<float> / 180.0f);
    const float aimAngle = std::acos(std::clamp(Dot(railForward, aimForward), -1.0f, 1.0f));
    if (aimAngle > neckMaxRad) {
      const Quaternion railToAim = MakeRotationFromTo(railForward, aimForward);
      const Quaternion bodyTurn = Slerp(Quaternion{0.0f, 0.0f, 0.0f, 1.0f}, railToAim,
                                        (aimAngle - neckMaxRad) / aimAngle);
      bodyForward = SafeNormalize(RotateVector(bodyTurn, railForward));
    }
    dragonModel_->GetWingAnimator().SetLookDirection(aimForward);
  }
  const Vector3 bodyRight = SafeNormalize(Cross(camera_->GetRailUp(), bodyForward));
  const Vector3 bodyUp = Cross(bodyForward, bodyRight);
  Matrix4x4 bodyBasis = MakeIdentity4x4();
  const Vector3 axes[3] = {bodyRight, bodyUp, bodyForward};
  for (int i = 0; i < 3; ++i) {
    bodyBasis.m[i][0] = axes[i].x;
    bodyBasis.m[i][1] = axes[i].y;
    bodyBasis.m[i][2] = axes[i].z;
  }
  // レールの曲がりで曲がる側へ傾け、登りで機首を上げる（ラジアン。ロールは右へ傾くのが正、機首は上げるのが正）
  const float kDegToRad = std::numbers::pi_v<float> / 180.0f;
  const float railBankRight =
      std::clamp(railTurnRate_ * actionConfig_.railBankPerTurn, -actionConfig_.railBankMax,
                 actionConfig_.railBankMax) *
      kDegToRad;
  const float railNoseUp = (std::max)(railClimb_, 0.0f) * actionConfig_.railClimbNose * kDegToRad;

  // 移動量とレールの曲がり・登りに応じて、自機の軸のまわりに傾ける（ピッチ: 右の軸で正が機首を下げる、ヨー: 上の軸、ロール: 前の軸で正が左へ傾く）
  const Vector3 motionTilt = {localVelY * actionConfig_.pitchStrength - railNoseUp,
                              localVelX * actionConfig_.yawStrength,
                              -localVelX * actionConfig_.rollStrength - railBankRight};
  const Quaternion targetOrientation = MakeRotationFromMatrix(
      Multiply(MakeRotateMatrix(motionTilt), bodyBasis));
  // 演出が向きを決めていた後は、そのときの角度の向きから続ける
  if (!useFlightOrientation_) {
    flightOrientation_ = MakeRotationFromMatrix(MakeRotateMatrix(transform_.rotate));
    useFlightOrientation_ = true;
  }
  flightOrientation_ =
      NormalizeRotation(Slerp(flightOrientation_, targetOrientation, kFlightTurnRate_));
  blendedOrientation_.reset();

  // 角度の向き（開始演出・ゲームオーバーなど、角度で自機の向きを引き継ぐ所が読む）
  // ヨーとピッチは照準の方向、ロールは移動量から決める
  const float aimYaw = std::atan2(toTarget.x, toTarget.z);
  const float aimPitch = std::atan2(
      -toTarget.y, std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z));
  const float targetRoll = -localVelX * actionConfig_.rollStrength;
  const float targetPitch = aimPitch + localVelY * actionConfig_.pitchStrength;
  const float targetYaw = aimYaw + localVelX * actionConfig_.yawStrength;

  // 差を -π〜π に丸めて、近い向きへ滑らかに近づける
  transform_.rotate.z =
      LerpAngle(transform_.rotate.z, targetRoll, actionConfig_.rollLerp);
  transform_.rotate.x =
      LerpAngle(transform_.rotate.x, targetPitch, actionConfig_.pitchLerp);
  transform_.rotate.y =
      LerpAngle(transform_.rotate.y, targetYaw, actionConfig_.yawLerp);
  // 角度を -π〜π に戻す
  const float kFullTurn = 2.0f * std::numbers::pi_v<float>;
  transform_.rotate.x = std::remainder(transform_.rotate.x, kFullTurn);
  transform_.rotate.y = std::remainder(transform_.rotate.y, kFullTurn);
  transform_.rotate.z = std::remainder(transform_.rotate.z, kFullTurn);

  UpdateTransform();

  // 射撃入力とステート管理
  if (input_) {
    // スペースキーまたはパッドの A を射撃/ロックオンボタンとする
    bool isTrigger =
        input_->IsTriggerKey(DIK_SPACE) || input_->IsPadTrigger(PadButton::A);
    bool isPress =
        input_->IsPressKey(DIK_SPACE) || input_->IsPadPress(PadButton::A);
    bool isRelease =
        input_->IsReleaseKey(DIK_SPACE) || input_->IsPadRelease(PadButton::A);

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
                    rPos, isLockOnMode, actionConfig_.lockOnRadius,
                    camera_->GetTranslate());
  }

  // 連続衝突判定用に速度を計算してコライダーに渡す
  if (collider_) {
    collider_->SetVelocity(CalculateVelocityForCollision());
  }
}

void Player::SetLockOnOccluders(const std::vector<Matrix4x4> *worldToBoxes) {
  if (lockOn_) {
    lockOn_->SetOccluders(worldToBoxes);
  }
}

bool Player::IsHiddenFromCamera(const Vector3 &pos) const {
  if (!lockOn_ || !camera_) {
    return false;
  }
  return lockOn_->IsHidden(camera_->GetTranslate(), pos);
}

bool Player::IsLockOnMode() const {
  return attackState_ == AttackState::LockOn;
}

void Player::FireHomingShot() {
  if (!lockOn_ || !object3dRenderer_ || !GetBodyObject())
    return;

  const std::vector<BaseActor *> targets = lockOn_->GetTargets();
  if (targets.empty())
    return;

  // 前の射撃でまだ出ていない弾は捨てるので、その射撃の弾としては終わりにする
  for (const HomingQueueItem &item : homingQueue_) {
    FinishVolleyBullet(item.volleyId);
  }

  // 射撃に番号を振り、発射キューにロックオン対象をすべて登録
  const int volleyId =
      scoreKeeper_ ? scoreKeeper_->BeginVolley(static_cast<int>(targets.size()))
                   : kNoVolleyId;
  homingQueue_.clear();
  homingQueue_.reserve(targets.size());
  for (size_t i = 0; i < targets.size(); ++i) {
    homingQueue_.push_back({targets[i], static_cast<int>(i),
                            static_cast<int>(targets.size()), volleyId});
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

  // 発射待ちの間に敵が撃破されていた場合は、狙う敵なしで撃つ
  BaseActor *validTarget = item.target.Get();
  if (validTarget && validTarget->IsDead()) {
    validTarget = nullptr;
  }

  SpawnHomingBullet(validTarget, item.index, item.totalCount, item.volleyId);
}

void Player::FinishVolleyBullet(int volleyId) {
  if (scoreKeeper_ && volleyId != kNoVolleyId) {
    scoreKeeper_->FinishVolleyBullet(volleyId);
  }
}

int Player::GetLockingCount() const {
  return lockOn_ ? static_cast<int>(lockOn_->GetTargets().size()) : 0;
}

Vector2 Player::GetReticlePosition() const {
  return reticleTunnel_ ? reticleTunnel_->Get2DPosition()
                        : Vector2{640.0f, 360.0f};
}

void Player::SpawnHomingBullet(BaseActor *target, int index, int totalCount,
                               int volleyId) {
  // 弾を出せないときは、射撃の弾としては終わりにする
  if (!object3dRenderer_ || !GetBodyObject() || !camera_) {
    FinishVolleyBullet(volleyId);
    return;
  }

  // カメラ空間の各基底ベクトル（正面、右、上）を取得
  Matrix4x4 viewMatrix = camera_->GetViewMatrix();
  Matrix4x4 cameraWorld = Inverse(viewMatrix);
  Vector3 cameraRight = {cameraWorld.m[0][0], cameraWorld.m[0][1], cameraWorld.m[0][2]};
  Vector3 cameraUp = {cameraWorld.m[1][0], cameraWorld.m[1][1], cameraWorld.m[1][2]};
  Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1], cameraWorld.m[2][2]};

  // 自機砲門の位置
  Vector3 playerPos = GetBodyObject()->GetTranslation();
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

  // 始まり P0: 砲門から初速の向きへ 8m 押し出した所
  Vector3 dir = Normalize(initialVelocity);
  Vector3 startPos = {muzzlePos.x + dir.x * 8.0f, muzzlePos.y + dir.y * 8.0f, muzzlePos.z + dir.z * 8.0f};

  // ふくらみ P1: P0 から、カメラから見た初速（初速 − 構図の進む速さ）でふくらみのフレーム数だけ進んだ所
  const Vector3 relativeVelocity = initialVelocity - framingVelocity_;
  Vector3 bulgePos = startPos + relativeVelocity * actionConfig_.homingBulgeFrames;

  // P0・P1 を構図から見た位置（右・上・前の m）にして渡す。弾は毎フレーム今の構図の上に置き直す
  const RailCamera::Framing &frame = camera_->GetFraming();
  auto toFramingLocal = [&frame](const Vector3 &world) {
    const Vector3 d = world - frame.position;
    return Vector3{Dot(d, frame.right), Dot(d, frame.up), Dot(d, frame.forward)};
  };

  // 3フレーム間隔で出し、どの弾も同じ秒で届くので、3フレームおきに当たる
  auto bullet = std::make_unique<HomingBullet>();
  bullet->Initialize(object3dRenderer_, camera_, toFramingLocal(startPos),
                     toFramingLocal(bulgePos), target,
                     actionConfig_.homingFlightSeconds);

  // 弾が消えたとき（着弾・散ったとき）にロックオン追尾マーカーを解除するコールバックを設定
  LockOn *lockOnPtr = lockOn_.get();
  std::weak_ptr<bool> isAliveWeak = isAliveToken_;
  bullet->SetOnDestroyCallback([lockOnPtr, isAliveWeak](BaseActor *t) {
    if (!isAliveWeak.expired() && lockOnPtr) {
      lockOnPtr->RemoveTrackingTarget(t);
    }
  });

  // 射撃の弾が終わったら数える係へ知らせる（係が先に消えていれば何もしない）
  if (scoreKeeper_ && volleyId != kNoVolleyId) {
    ScoreKeeper *keeper = scoreKeeper_;
    std::weak_ptr<bool> keeperAlive = scoreKeeper_->GetAliveToken();
    bullet->SetVolley(volleyId, [keeper, keeperAlive](int id) {
      if (!keeperAlive.expired()) {
        keeper->FinishVolleyBullet(id);
      }
    });
  }

  ActorManager::GetInstance()->AddActor(std::move(bullet));

  // 発射音を小刻みに再生
  SoundManager::GetInstance()->PlaySE("lockon_fire");
}

void Player::FireNormalShot() {
  if (!object3dRenderer_ || !GetBodyObject())
    return;

  // プレイヤーのワールド行列を取得
  Matrix4x4 playerWorld = GetBodyObject()->GetWorldMatrix();

  // 口元のローカル座標（X, Y, Z）を作成
  Vector3 localMuzzle = {actionConfig_.muzzleOffsetX, actionConfig_.muzzleOffsetY,
                         actionConfig_.muzzleOffsetForward};

  // 竜のモデルがあれば、照準の方を向いている頭から出す。なければ体の口元のオフセットの位置から出す
  Vector3 startPos = dragonModel_ ? dragonModel_->GetHeadPosition()
                                  : TransformPoint(localMuzzle, playerWorld);

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

  // 弾が向かう地点を決める
  // レティクルに重なっている相手（ロックオン対象と、撃ち落とせる敵の弾）がいればその相手、
  // いなければカメラからレティクル方向へ一定距離の点
  Vector3 aimRayDir = Normalize(targetPos - cameraPos);
  targetPos = cameraPos + aimRayDir * kAimConvergenceDistance_;

  Vector2 reticlePos = reticleTunnel_ ? reticleTunnel_->Get2DPosition()
                                      : Vector2{640.0f, 360.0f};
  Matrix4x4 viewProj = camera_->GetViewProjectionMatrix();
  float nearestDepth = (std::numeric_limits<float>::max)();
  std::vector<BaseActor *> aimTargets = lockOnTargets_;
  aimTargets.insert(aimTargets.end(), shotTargets_.begin(), shotTargets_.end());
  for (BaseActor *target : aimTargets) {
    if (!target || target->IsDead()) {
      continue;
    }
    Vector3 enemyPos = target->GetTransform().translate;
    // カメラより後ろの敵は画面座標が反転するので除外する
    float depth = Dot(enemyPos - cameraPos, cameraForward);
    if (depth <= 0.0f) {
      continue;
    }
    // 画面上でレティクルに重なっている相手のうち、いちばん手前の相手を狙う
    Vector2 screenPos = WorldToScreen(enemyPos, viewProj, 1280.0f, 720.0f);
    float dx = screenPos.x - reticlePos.x;
    float dy = screenPos.y - reticlePos.y;
    if (dx * dx + dy * dy <= kAimAssistScreenRadius_ * kAimAssistScreenRadius_ &&
        depth < nearestDepth) {
      nearestDepth = depth;
      targetPos = enemyPos;
    }
  }

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

void Player::SetReticleAlpha(float alpha) {
  if (reticleTunnel_) {
    reticleTunnel_->SetAlpha(alpha);
  }
}

void Player::Draw3D() {
  // 無敵時間中の点滅
  if (invincibleTimer_ > 0 && (invincibleTimer_ / 4) % 2 == 0) {
    return;
  }

  // プレイヤーの描画処理
  if (GetBodyObject()) {
    GetBodyObject()->Draw();
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

void Player::Kill() {
  invincibleTimer_ = 0;
  TakeDamage(hp_);
}

void Player::SetDragonModel(std::unique_ptr<DragonModel> model) {
  dragonModel_ = std::move(model);
}

Vector3 Player::GetHeadPosition() const {
  return dragonModel_ ? dragonModel_->GetHeadPosition() : transform_.translate;
}

Object3d *Player::GetBodyObject() const {
  return dragonModel_ ? dragonModel_->GetObject3d() : object3d_.get();
}

void Player::SetThreatGlow(float amount) {
  Object3d *body = GetBodyObject();
  if (!body) {
    return;
  }
  // 体の色を赤へ寄せ、ふちを赤く光らせる。0 なら白とふちの光なしに戻す
  amount = std::clamp(amount, 0.0f, 1.0f);
  const float mix = amount * kThreatBodyMix_;
  body->SetColor({1.0f + (kThreatBodyColor_.x - 1.0f) * mix, 1.0f + (kThreatBodyColor_.y - 1.0f) * mix,
                  1.0f + (kThreatBodyColor_.z - 1.0f) * mix, 1.0f});
  body->SetRimLight(kThreatRimColor_, kThreatRimIntensity_ * amount, kThreatRimPower_);
}

void Player::SetWingGesture(const DragonWingAnimator::Gesture &gesture) {
  if (dragonModel_) {
    dragonModel_->GetWingAnimator().SetGesture(gesture);
  }
}

void Player::DrawWingConfigImGui() {
  if (dragonModel_) {
    dragonModel_->GetWingAnimator().DrawImGui();
  }
}

void Player::SetDirectWingRoot(
    DragonWingAnimator::DirectRootFunction rootAngleAt) {
  if (dragonModel_) {
    dragonModel_->GetWingAnimator().SetDirectRoot(std::move(rootAngleAt));
  }
}

float Player::GetWingPush(bool isRight) const {
  if (!dragonModel_) {
    return 0.0f;
  }
  return dragonModel_->GetWingAnimator().GetDownstrokePush(isRight);
}

float Player::GetWingRootAngle(bool isRight) const {
  if (!dragonModel_) {
    return 0.0f;
  }
  return dragonModel_->GetWingAnimator().GetRootAngle(isRight);
}

float Player::GetFaceAngle() const {
  if (!dragonModel_) {
    return 0.0f;
  }
  return dragonModel_->GetWingAnimator().GetFaceAngle();
}

void Player::UpdateWing(float deltaTime) {
  if (!dragonModel_) {
    return;
  }
  dragonModel_->SetWingState(externalWingState_ ? *externalWingState_
                                                : flightWingState_);
  dragonModel_->Update(deltaTime);
}

void Player::UseExternalTransform() {
  useFlightOrientation_ = false;
  hasFramingOffset_ = false;
  hasPrevRailForward_ = false;
  // 頭は演出の体の向きに沿わせる
  if (dragonModel_) {
    dragonModel_->GetWingAnimator().SetLookDirection(std::nullopt);
  }
}

void Player::BlendOrientationFrom(const Vector3 &rotate, float ratio) {
  if (!useFlightOrientation_) {
    return;
  }
  const Quaternion from = MakeRotationFromMatrix(MakeRotateMatrix(rotate));
  blendedOrientation_ =
      NormalizeRotation(Slerp(from, flightOrientation_, std::clamp(ratio, 0.0f, 1.0f)));
}

void Player::UpdateTransform() {
  if (GetBodyObject()) {
    // カメラの正面方向を取得
    Matrix4x4 viewMatrix = camera_->GetViewMatrix();
    Matrix4x4 cameraWorld = Inverse(viewMatrix);
    Vector3 cameraForward = {cameraWorld.m[2][0], cameraWorld.m[2][1],
                             cameraWorld.m[2][2]};

    GetBodyObject()->SetTranslation(transform_.translate);
    GetBodyObject()->SetRotation(transform_.rotate);
    if (useFlightOrientation_) {
      GetBodyObject()->SetRotationQuaternion(blendedOrientation_.value_or(flightOrientation_));
    } else {
      GetBodyObject()->ClearRotationQuaternion();
    }
    GetBodyObject()->SetScale(transform_.scale);
    GetBodyObject()->Update();
  }
}

void Player::ForceSnapToCamera() {

  float ndcX = 0.0f;
  float ndcY = 0.0f;
  if (reticleTunnel_) {
    ndcX = (reticleTunnel_->Get2DPosition().x / 1280.0f) * 2.0f - 1.0f;
    ndcY = 1.0f - (reticleTunnel_->Get2DPosition().y / 720.0f) * 2.0f;
  }

  // 自機を置く土台はカメラの構図（Update と同じ）
  const RailCamera::Framing &frame = camera_->GetFraming();

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

  const Vector2 targetLocal = camera_->ToFramingOffset(
      {moveRatioX * actionConfig_.playerMaxMoveX, moveY + actionConfig_.playerBaseOffsetY});
  const Vector3 targetPos = frame.position + frame.forward * distance +
                            frame.right * targetLocal.x + frame.up * targetLocal.y;

  transform_.translate = targetPos;
  framingOffset_ = targetLocal;
  hasFramingOffset_ = true;
  framingVelocity2D_ = {0.0f, 0.0f};
  UpdateTransform();
}

void Player::SaveActionConfig() {
  nlohmann::json root;
  root["lockOnRadius"] = actionConfig_.lockOnRadius;
  root["homingFlightSeconds"] = actionConfig_.homingFlightSeconds;
  root["homingBulgeFrames"] = actionConfig_.homingBulgeFrames;
  root["reticleAcceleration"] = actionConfig_.reticleAcceleration;
  root["reticleFriction"] = actionConfig_.reticleFriction;
  root["reticleMaxSpeed"] = actionConfig_.reticleMaxSpeed;
  root["playerFollowTime"] = actionConfig_.playerFollowTime;
  root["playerFollowDamping"] = actionConfig_.playerFollowDamping;
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
  root["wingStrength"] = actionConfig_.wingStrength;
  root["wingFrequency"] = actionConfig_.wingFrequency;
  root["wingClimbStrength"] = actionConfig_.wingClimbStrength;
  root["wingDiveFold"] = actionConfig_.wingDiveFold;
  root["wingTurnAsymmetry"] = actionConfig_.wingTurnAsymmetry;
  root["wingHover"] = actionConfig_.wingHover;
  root["wingClimbHover"] = actionConfig_.wingClimbHover;
  root["railBankPerTurn"] = actionConfig_.railBankPerTurn;
  root["railBankMax"] = actionConfig_.railBankMax;
  root["railClimbNose"] = actionConfig_.railClimbNose;
  root["railLagPerTurn"] = actionConfig_.railLagPerTurn;
  root["railLagPerClimb"] = actionConfig_.railLagPerClimb;
  root["railFollow"] = actionConfig_.railFollow;

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
      if (root.contains("homingFlightSeconds"))
        actionConfig_.homingFlightSeconds = root["homingFlightSeconds"];
      if (root.contains("homingBulgeFrames"))
        actionConfig_.homingBulgeFrames = root["homingBulgeFrames"];
      if (root.contains("reticleAcceleration"))
        actionConfig_.reticleAcceleration = root["reticleAcceleration"];
      if (root.contains("reticleFriction"))
        actionConfig_.reticleFriction = root["reticleFriction"];
      if (root.contains("reticleMaxSpeed"))
        actionConfig_.reticleMaxSpeed = root["reticleMaxSpeed"];
      if (root.contains("playerFollowTime"))
        actionConfig_.playerFollowTime = root["playerFollowTime"];
      if (root.contains("playerFollowDamping"))
        actionConfig_.playerFollowDamping = root["playerFollowDamping"];
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
      if (root.contains("wingStrength"))
        actionConfig_.wingStrength = root["wingStrength"];
      if (root.contains("wingFrequency"))
        actionConfig_.wingFrequency = root["wingFrequency"];
      if (root.contains("wingClimbStrength"))
        actionConfig_.wingClimbStrength = root["wingClimbStrength"];
      if (root.contains("wingDiveFold"))
        actionConfig_.wingDiveFold = root["wingDiveFold"];
      if (root.contains("wingTurnAsymmetry"))
        actionConfig_.wingTurnAsymmetry = root["wingTurnAsymmetry"];
      if (root.contains("wingHover"))
        actionConfig_.wingHover = root["wingHover"];
      if (root.contains("wingClimbHover"))
        actionConfig_.wingClimbHover = root["wingClimbHover"];
      if (root.contains("railBankPerTurn"))
        actionConfig_.railBankPerTurn = root["railBankPerTurn"];
      if (root.contains("railBankMax"))
        actionConfig_.railBankMax = root["railBankMax"];
      if (root.contains("railClimbNose"))
        actionConfig_.railClimbNose = root["railClimbNose"];
      if (root.contains("railLagPerTurn"))
        actionConfig_.railLagPerTurn = root["railLagPerTurn"];
      if (root.contains("railLagPerClimb"))
        actionConfig_.railLagPerClimb = root["railLagPerClimb"];
      if (root.contains("railFollow"))
        actionConfig_.railFollow = root["railFollow"];

      isActionConfigDirty_ = false; // ロード成功時のみ未保存フラグをリセット
    } catch (...) {
      // Parse error, keep defaults
    }
  }
}
