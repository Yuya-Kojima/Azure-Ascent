#include "NormalBullet.h"
#include "Render/Object3d/Object3d.h"
#include "Model/Model.h"
#include "Model/ModelManager.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/ModelRenderer.h"
#include "Math/MathUtil.h"
#include "Debug/Logger.h"
#include <Windows.h>
#include "Actor/Enemy.h"
#include "Render/Renderer/LineRenderer.h"
#include <cmath>
#include "Collision/SphereCollider.h"

#include "Collision/CollisionManager.h"
#include "Effect/EffectManager.h"

NormalBullet::NormalBullet() {}
NormalBullet::~NormalBullet() {
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void NormalBullet::Initialize(Object3dRenderer* renderer, const Vector3& startPos, const Vector3& velocity) {
  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(renderer);
  
  // エンジンのビルトインカプセルモデルを使用
  object3d_->SetModel("__builtin_capsule");
  object3d_->SetScale({1.5f, 10.0f, 1.5f});      // 直径1.5m, 長さ20mのレーザービーム（Y軸が長手方向）
  object3d_->SetEnableLighting(false);           // ライティングOFF（自己発光モード）
  object3d_->SetColor({3.0f, 1.5f, 0.2f, 1.0f}); // 鮮やかなオレンジイエローオーラ
  object3d_->SetTranslation(startPos);

  velocity_ = velocity; // 目標へのベクトル
  lifeTimer_ = 180; 

  // 進行方向（初速）へ弾頭を向ける（縦向きY軸モデルを倒してベクトルへ向ける）
  float lenSq = velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z;
  if (lenSq > 0.0001f) {
    float yaw = std::atan2(velocity_.x, velocity_.z);
    float xzLen = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    float pitch = std::atan2(-velocity_.y, xzLen);
    // 初期状態が上向き(+Y)のカプセルの頭を進行方向(+Z方向基準)へ90度倒す
    Vector3 rot = {pitch + (std::numbers::pi_v<float> * 0.5f), yaw, 0.0f};
    object3d_->SetRotation(rot);
    transform_.rotate = rot;
  }

  // コライダーの設定（当てやすい元の2.0f）
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(2.0f);
  collider_->SetAttribute(kCollisionAttributePlayerBullet);
  collider_->SetMask(kCollisionAttributeEnemy | kCollisionAttributeEnemyBullet);
  collider_->SetVelocity(velocity_);
  CollisionManager::GetInstance()->Register(collider_.get());
}

void NormalBullet::Update() {
  if (isDead_) return;

  lifeTimer_--;
  if (lifeTimer_ <= 0) {
    isDead_ = true;
  }

  // 座標を更新（ホーミングせず直進のみ）
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

  object3d_->Update();
}

void NormalBullet::OnCollision(Collider* other) {
  if (isDead_) return;

  if (other->GetAttribute() & kCollisionAttributeEnemy) {
    Enemy* enemy = dynamic_cast<Enemy*>(other->GetOwner());
    if (enemy && !enemy->IsDead()) {
      enemy->SetLastHitVolley(kNoVolleyId);
      enemy->TakeDamage(damage_);
      isDead_ = true;

      // 着弾ヒットスパーク（通常弾：ゴールド/オレンジ系の火花）
      EffectManager::GetInstance()->PlayEffect(
          EffectType::HitSpark,
          object3d_->GetTranslation(),
          Vector4{2.0f, 1.4f, 0.4f, 1.0f}
      );

      Logger::Log("Normal Bullet Hit Enemy!\n");
    }
  }
  else if (other->GetAttribute() & kCollisionAttributeEnemyBullet) {
    isDead_ = true;

    // 着弾ヒットスパーク（通常弾：ゴールド/オレンジ系の火花）
    EffectManager::GetInstance()->PlayEffect(
        EffectType::HitSpark,
        object3d_->GetTranslation(),
        Vector4{2.0f, 1.4f, 0.4f, 1.0f}
    );

    Logger::Log("Normal Bullet Intercepted EnemyBullet!\n");
  }
}

void NormalBullet::Draw3D() {
  if (!isDead_ && object3d_) {
    object3d_->Draw();

#ifdef USE_IMGUI
    // ==== デバッグ描画 ====
    LineRenderer* lineRenderer = LineRenderer::GetInstance();
    int segments = 16;
    float angleStep = 2.0f * 3.14159265f / segments;
    Vector4 color = {0.0f, 0.0f, 1.0f, 1.0f}; 
    float radius = 0.5f;
    Vector3 pos = object3d_->GetTranslation();

    for (int i = 0; i < segments; ++i) {
      float angle1 = i * angleStep;
      float angle2 = (i + 1) * angleStep;

      // XY plane
      Vector3 p1_xy = {pos.x + std::cos(angle1) * radius, pos.y + std::sin(angle1) * radius, pos.z};
      Vector3 p2_xy = {pos.x + std::cos(angle2) * radius, pos.y + std::sin(angle2) * radius, pos.z};
      lineRenderer->DrawLine(p1_xy, p2_xy, color);

      // XZ plane
      Vector3 p1_xz = {pos.x + std::cos(angle1) * radius, pos.y, pos.z + std::sin(angle1) * radius};
      Vector3 p2_xz = {pos.x + std::cos(angle2) * radius, pos.y, pos.z + std::sin(angle2) * radius};
      lineRenderer->DrawLine(p1_xz, p2_xz, color);

      // YZ plane
      Vector3 p1_yz = {pos.x, pos.y + std::cos(angle1) * radius, pos.z + std::sin(angle1) * radius};
      Vector3 p2_yz = {pos.x, pos.y + std::cos(angle2) * radius, pos.z + std::sin(angle2) * radius};
      lineRenderer->DrawLine(p1_yz, p2_yz, color);
    }
#endif
  }
}
