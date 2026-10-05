#include "Actor/Enemy.h"
#include "Audio/SoundManager.h"
#include "Actor/Player.h"
#include "Behavior/IEnemyBehavior.h"
#include "Camera/ICamera.h"
#include "Camera/RailCamera.h"
#include "Collision/CollisionManager.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Effect/EffectManager.h"
#include "Effect/EnemyDebris.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Particle/IParticleEmitter.h"
#include "Render/Particle/ParticleEmitter.h"
#include "Render/Particle/ParticleManager.h"
#include <cmath>
#include <numbers>

Enemy::Enemy() = default;

Enemy::~Enemy() {
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void Enemy::Initialize() {
  // 敵のコライダーの初期化
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(
      0.8f); // 敵の当たり判定の大きさをモデルより少し小さめに設定
  collider_->SetAttribute(kCollisionAttributeEnemy); // 自機から見て「敵」
  collider_->SetMask(kCollisionAttributePlayer |
                     kCollisionAttributePlayerBullet); // 自機や自機の弾と当たる
  CollisionManager::GetInstance()->Register(collider_.get());
}

void Enemy::Update() {
  // 死んでいる場合は当たり判定を消して何もさせない
  if (isDead_) {
    if (collider_) {
      collider_->SetEnable(false); // コライダーを無効化（メモリは破棄しない）
    }
    return;
  }

  // --- カメラ基準値の毎フレーム更新 ---
  if (camera_) {
    if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
      basePos_ = railCam->GetRailPosition();
      baseForward_ = railCam->GetRailForward();
      baseRight_ = railCam->GetRailRight();
      baseUp_ = railCam->GetRailUp();
    } else {
      basePos_ = camera_->GetTranslate();
      baseForward_ = camera_->GetForward();
      baseRight_ = camera_->GetRight();
      baseUp_ = camera_->GetUp();
    }
  }

  // 完全に画面外に出て、かつカメラ後方に取り残された場合のみ自動消滅（ボスは除く）
  if (!IsBoss() && camera_) {
    // 画面内に映っている間は絶対に消さない（視界内での急な消滅を防止）
    if (!IsInScreen(80.0f)) {
      Vector3 diff = {transform_.translate.x - basePos_.x,
                      transform_.translate.y - basePos_.y,
                      transform_.translate.z - basePos_.z};
      float forwardDist = diff.x * baseForward_.x + diff.y * baseForward_.y +
                          diff.z * baseForward_.z;
      // 画面外かつカメラの後方（-10m以上後ろ）で、出現から2秒以上経過していれば安全にデスポーン
      if (aliveTime_ > 2.0f && forwardDist < -10.0f) {
        if (collider_) {
          collider_->SetEnable(false);
        }
        Destroy();
        return;
      }
    }
  }

  aliveTime_ += 1.0f / 60.0f; // 簡易的に60FPS固定で時間計算

  if (behavior_) {
    behavior_->Update(this);
  }

  // 被弾時は赤色にする
  if (hitFlashTimer_ > 0) {
    hitFlashTimer_--;
    if (model_) {
      model_->SetColor({1.0f, 0.0f, 0.0f, 1.0f}); // 赤色
    }
  } else {
    if (model_) {
      model_->SetColor(baseColor_); // 元の色
    }
  }
  // モデルの更新
  UpdateTransform();

  // 連続衝突判定用に速度を計算してコライダーに渡す
  if (collider_) {
    collider_->SetVelocity(CalculateVelocityForCollision());
  }
}

void Enemy::UpdateTransform() {
  // モデルが存在していれば、敵の座標をモデルに反映して更新
  if (model_) {
    model_->SetTranslation(transform_.translate);
    model_->SetRotation(transform_.rotate);

    // 出現時ポップイン演出（スポーン直後の0.25秒間で 0.0 -> 1.0 へ拡大実体化）
    Vector3 finalScale = transform_.scale;
    if (aliveTime_ < 0.25f) {
      float t = aliveTime_ / 0.25f;
      float scaleFactor = std::sin(t * (std::numbers::pi_v<float> * 0.5f));
      finalScale = {transform_.scale.x * scaleFactor,
                    transform_.scale.y * scaleFactor,
                    transform_.scale.z * scaleFactor};
    }

    model_->SetScale(finalScale);
    model_->Update();
  }
}

void Enemy::Draw3D() {
  // 3Dモデルの描画
  if (model_) {
    model_->Draw();
  }
}

void Enemy::OnCollision(Collider *other) {
  // プレイヤーと衝突した場合、自身もダメージを受けて自爆する
  if (other->GetOwner() && dynamic_cast<Player *>(other->GetOwner())) {
    Player *p = dynamic_cast<Player *>(other->GetOwner());
    if (p->GetInvincibleTimer() > 0) {
      return; // 無敵中なら食らわない
    }
    p->TakeDamage(1); // プレイヤーにダメージを与える
    Logger::Log("Enemy Self-Destruct into Player!\n");
    TakeDamage(999, true); // true を渡して自爆であることを知らせる
  }
}

void Enemy::TakeDamage(int damage, bool isSelfDestruct) {
  if (isDead_) {
    return;
  }

  hp_ -= damage;
  hitFlashTimer_ = 5; // 5フレーム点滅

  if (hp_ <= 0) {
    Logger::Log("Enemy Destroyed!\n");

    // 自爆でない場合、自律的に爆発する
    if (!isSelfDestruct) {
      SoundManager::GetInstance()->PlaySE("enemy_destroy");
      // 死亡時エフェクト
      EffectManager::GetInstance()->PlayEnemyDeathSimpleEffect(
          transform_.translate, baseColor_);

      // 敵のカラーとサイズを引き継いだ破片（デブリ）を発生させて四方に弾け飛ばす
      auto renderer = PrefabManager::GetInstance()->GetObject3dRenderer();
      if (renderer) {
        auto debris = std::make_unique<EnemyDebris>(
            renderer, transform_.translate, transform_.scale, baseColor_);
        ActorManager::GetInstance()->AddActor(std::move(debris));
      }
    }

    if (onDestroyedCallback_) {
      onDestroyedCallback_(isSelfDestruct);
    }
    Destroy();
  } else {
    SoundManager::GetInstance()->PlaySE_Once("enemy_hit");
  }
}

bool Enemy::IsInScreen(float margin) const {
  if (!camera_) {
    return false;
  }
  Vector2 screenPos = WorldToScreen(
      transform_.translate, camera_->GetViewProjectionMatrix(), 1280.0f, 720.0f);
  // カメラの後方にいる場合（透視投影除算のw <= 0）は画面外
  if (screenPos.x < -5000.0f || screenPos.y < -5000.0f) {
    return false;
  }
  // 画面枠（マージン付き）に入っているか判定
  if (screenPos.x >= -margin && screenPos.x <= 1280.0f + margin &&
      screenPos.y >= -margin && screenPos.y <= 720.0f + margin) {
    return true;
  }
  return false;
}
