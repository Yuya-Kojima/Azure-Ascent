#include "HomingBullet.h"
#include "Actor/Enemy.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/LineRenderer.h"
#include <Windows.h>
#include <cmath>

#include "Collision/CollisionManager.h"
#include "Renderer/TrailRenderer.h"
#include "Render/Camera/ICamera.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Effect/EffectManager.h"

HomingBullet::HomingBullet() {}
HomingBullet::~HomingBullet() {
  FinishVolley();
  if (onDestroyCallback_) {
    onDestroyCallback_(originalTarget_);
  }
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void HomingBullet::Initialize(Object3dRenderer *renderer,
                              const Vector3 &startPos, BaseActor *target,
                              const Vector3 &initialVelocity) {
  renderer_ = renderer;
  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(renderer);

  object3d_->SetModel("__builtin_capsule");
  // 弾頭モデル：トレイル先端と繋げるためのコア（直径0.5m、長さ4m）
  object3d_->SetScale({0.5f, 4.0f, 0.5f});
  object3d_->SetEnableLighting(false);
  object3d_->SetColor({0.3f, 3.5f, 5.0f, 1.0f}); // シアン（自己発光）
  object3d_->SetTranslation(startPos);

  velocity_ = initialVelocity;
  target_ = target;
  originalTarget_ = target;
  lifeTimer_ = 180;

  // コライダーの設定
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(1.0f);
  collider_->SetAttribute(kCollisionAttributePlayerBullet);
  collider_->SetMask(kCollisionAttributeEnemy | kCollisionAttributeEnemyBullet);
  collider_->SetVelocity(velocity_);
  CollisionManager::GetInstance()->Register(collider_.get());

  // 初速ベクトルから初期回転を計算
  float lenSq = velocity_.x * velocity_.x + velocity_.y * velocity_.y +
                velocity_.z * velocity_.z;
  if (lenSq > 0.0001f) {
    float yaw = std::atan2(velocity_.x, velocity_.z);
    float xzLen =
        std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    float pitch = std::atan2(-velocity_.y, xzLen);
    Vector3 rot = {pitch + (std::numbers::pi_v<float> * 0.5f), yaw, 0.0f};
    object3d_->SetRotation(rot);
    transform_.rotate = rot;
  }

  object3d_->Update();
}

void HomingBullet::Update() {
  if (isDead_)
    return;

  lifeTimer_--;
  if (lifeTimer_ <= 0) {
    isDead_ = true;
    return;
  }

  // ターゲットが生きていれば誘導ベクトルを計算
  if (target_) {
    if (target_->IsDead()) {
      target_ =
          nullptr; // ターゲットが死んだら誘導をやめ、そのまま直進・落下させる
    }
  }
  // 狙う敵がいなければ、射撃の弾としては終わり
  if (!target_) {
    FinishVolley();
  }

  if (target_) {
    Vector3 currentPos = object3d_->GetTranslation();
    Vector3 targetPos = target_->GetTransform().translate;

    Vector3 toTarget = {targetPos.x - currentPos.x, targetPos.y - currentPos.y,
                        targetPos.z - currentPos.z};

    // 発射直後の拡散ディレイ時間が終了したら、ターゲットに向けて誘導を開始
    if (lifeTimer_ <= homingFallTime_) {
      Vector3 normToTarget = Normalize(toTarget);
      Vector3 desiredVelocity = {normToTarget.x * speed_,
                                 normToTarget.y * speed_,
                                 normToTarget.z * speed_};

      velocity_ = Lerp(velocity_, desiredVelocity, homingStrength_);

      // 滑らかに弧を描きながら敵へ突き刺さる
      homingStrength_ += homingStrengthIncrease_;
      if (homingStrength_ > homingStrengthMax_) {
        homingStrength_ = homingStrengthMax_;
      }
    }
  }

  // 座標を更新
  Vector3 pos = object3d_->GetTranslation();
  pos.x += velocity_.x;
  pos.y += velocity_.y;
  pos.z += velocity_.z;
  object3d_->SetTranslation(pos);
  // コライダーの中心座標を同期させる
  transform_.translate = pos;

  // コライダーへの速度反映
  if (collider_) {
    collider_->SetVelocity(velocity_);
  }

  // 弾の向き（回転）を進行方向（velocity_）に向ける
  float lenSq = velocity_.x * velocity_.x + velocity_.y * velocity_.y +
                velocity_.z * velocity_.z;
  if (lenSq > 0.0001f) {
    float yaw = std::atan2(velocity_.x, velocity_.z);
    float xzLen =
        std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    float pitch = std::atan2(-velocity_.y, xzLen);
    Vector3 rot = {pitch + (std::numbers::pi_v<float> * 0.5f), yaw, 0.0f};
    object3d_->SetRotation(rot);
    transform_.rotate = rot;
  }

  object3d_->Update();

  // レーザートレイル履歴の記録（弾頭位置）
  trailHistory_.insert(trailHistory_.begin(), pos);
  if (trailHistory_.size() > kMaxTrailPoints) {
    trailHistory_.pop_back();
  }
}

void HomingBullet::OnCollision(Collider *other) {
  if (isDead_)
    return;

  BaseActor *owner = other->GetOwner();
  if (!owner)
    return;

  // ロックオンターゲットが存在する場合、指定ターゲット以外はすべて貫通（すり抜け）する
  if (target_) {
    if (owner != target_) {
      return;
    }
  }

  // ターゲットに到達（またはターゲット喪失後の直進弾）
  if (other->GetAttribute() & kCollisionAttributeEnemy) {
    Enemy *enemy = dynamic_cast<Enemy *>(owner);
    if (enemy && !enemy->IsDead()) {
      enemy->SetLastHitVolley(volleyId_);
      enemy->TakeDamage(damage_);
      isDead_ = true;
      FinishVolley();

      // ホーミング専用着弾演出（太く鋭い十字グリント＋高速プラズマリング）
      EffectManager::GetInstance()->PlayEffect(
          EffectType::HomingHit,
          object3d_->GetTranslation(),
          Vector4{0.6f, 2.5f, 4.0f, 1.0f}
      );

      Logger::Log("Homing Bullet Hit Target Enemy!\n");
    }
  } else if (other->GetAttribute() & kCollisionAttributeEnemyBullet) {
    isDead_ = true;
    FinishVolley();

    // ホーミング専用着弾演出（迎撃時）
    EffectManager::GetInstance()->PlayEffect(
        EffectType::HomingHit,
        object3d_->GetTranslation(),
        Vector4{0.6f, 2.5f, 4.0f, 1.0f}
    );

    Logger::Log("Homing Bullet Intercepted Target EnemyBullet!\n");
  }
}

void HomingBullet::FinishVolley() {
  if (hasFinishedVolley_) {
    return;
  }
  hasFinishedVolley_ = true;
  if (onVolleyFinished_) {
    onVolleyFinished_(volleyId_);
  }
}

void HomingBullet::Draw3D() {
  if (!isDead_ && object3d_) {
    object3d_->Draw();

#ifdef USE_IMGUI
    // ==== デバッグ描画 ====
    LineRenderer *lineRenderer = LineRenderer::GetInstance();
    int segments = 16;
    float angleStep = 2.0f * 3.14159265f / segments;
    Vector4 color = {0.0f, 1.0f, 1.0f, 1.0f};
    float radius = 0.5f;
    Vector3 pos = object3d_->GetTranslation();

    for (int i = 0; i < segments; ++i) {
      float angle1 = i * angleStep;
      float angle2 = (i + 1) * angleStep;

      // XY plane
      Vector3 p1_xy = {pos.x + std::cos(angle1) * radius,
                       pos.y + std::sin(angle1) * radius, pos.z};
      Vector3 p2_xy = {pos.x + std::cos(angle2) * radius,
                       pos.y + std::sin(angle2) * radius, pos.z};
      lineRenderer->DrawLine(p1_xy, p2_xy, color);

      // XZ plane
      Vector3 p1_xz = {pos.x + std::cos(angle1) * radius, pos.y,
                       pos.z + std::sin(angle1) * radius};
      Vector3 p2_xz = {pos.x + std::cos(angle2) * radius, pos.y,
                       pos.z + std::sin(angle2) * radius};
      lineRenderer->DrawLine(p1_xz, p2_xz, color);

      // YZ plane
      Vector3 p1_yz = {pos.x, pos.y + std::cos(angle1) * radius,
                       pos.z + std::sin(angle1) * radius};
      Vector3 p2_yz = {pos.x, pos.y + std::cos(angle2) * radius,
                       pos.z + std::sin(angle2) * radius};
      lineRenderer->DrawLine(p1_yz, p2_yz, color);
    }
#endif
  }

  // レーザーのリボントレイル描画登録
  if (trailHistory_.size() >= 2) {
    std::vector<TrailRenderer::TrailNode> nodes;
    nodes.reserve(trailHistory_.size());

    size_t count = trailHistory_.size();
    for (size_t i = 0; i < count; ++i) {
      float t = static_cast<float>(i) / static_cast<float>(count - 1); // 0.0(弾頭) 〜 1.0(末尾)
      // 太さ：弾頭直後（0.8m）から末尾（0.05m）へ線形補間
      float width = Lerp(0.8f, 0.05f, t);
      // アルファ：末尾に向かって滑らかに減衰
      float alpha = 1.0f - t;
      // シアン（末尾フェードアウト）
      Vector4 color = {0.2f * alpha, 3.5f * alpha, 4.5f * alpha, alpha};

      nodes.push_back({trailHistory_[i], width, color});
    }

    // カメラ座標の取得
    Vector3 cameraPos = {0.0f, 0.0f, 0.0f};
    if (renderer_ && renderer_->GetDefaultCamera()) {
      cameraPos = renderer_->GetDefaultCamera()->GetTranslate();
    }

    TrailRenderer::GetInstance()->AddTrail(nodes, cameraPos);
  }
}
