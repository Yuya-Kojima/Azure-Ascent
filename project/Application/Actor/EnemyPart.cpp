#include "Actor/EnemyPart.h"
#include "Actor/Behavior/BehaviorTurret.h"
#include "Actor/EnemyBullet.h"
#include "Actor/Player.h"
#include "Camera/RailCamera.h"
#include "Collision/SphereCollider.h"
#include "Framework/ActorManager.h"
#include "Framework/PrefabManager.h"
#include "Math/MathUtil.h"
#include <algorithm>

EnemyPart::EnemyPart() = default;
EnemyPart::~EnemyPart() = default;

void EnemyPart::Setup(IEnemyPartOwner *owner, int index, Role role,
                      const Vector3 &localPosition, const Vector3 &halfSize) {
  owner_ = owner;
  index_ = index;
  role_ = role;
  localPosition_ = localPosition;
  hp_ = 1;
  transform_.scale = halfSize * 2.0f;
  baseScale_ = transform_.scale;
  fireTimer_ = kFireInterval_ - kFirstShotDelay_;
  if (collider_) {
    // 当たりの半径は自分の大きさ（transform_.scale）の最大の辺で掛けられるので、割っておいて一番短い半分の長さにする
    const float maxScale = (std::max)({transform_.scale.x, transform_.scale.y, transform_.scale.z});
    const float minHalf = (std::min)({halfSize.x, halfSize.y, halfSize.z});
    collider_->SetRadius(minHalf / maxScale);
  }
  if (owner_) {
    transform_.translate = TransformPoint(localPosition_, owner_->GetPartFrame(index_));
    transform_.rotate = owner_->GetPartRotation(index_);
  }
}

void EnemyPart::Update() {
  if (!IsDead() && owner_) {
    transform_.translate = TransformPoint(localPosition_, owner_->GetPartFrame(index_));
    transform_.rotate = owner_->GetPartRotation(index_);
    UpdateFire();
  }
  Enemy::Update();
  // 撃てない間は、自機の弾が当たらない
  if (!IsDead() && collider_) {
    collider_->SetEnable(IsActive());
  }
}

void EnemyPart::TakeDamage(int damage, bool isSelfDestruct) {
  if (IsDead() || (!isSelfDestruct && !IsActive())) {
    return;
  }
  Enemy::TakeDamage(damage, isSelfDestruct);
  if (IsDead() && owner_) {
    IEnemyPartOwner *owner = owner_;
    owner_ = nullptr;
    owner->OnPartDestroyed(index_);
  }
}

void EnemyPart::Vanish() {
  owner_ = nullptr;
  if (collider_) {
    collider_->SetEnable(false);
  }
  Destroy();
}

void EnemyPart::UpdateFire() {
  if (role_ != Role::Turret || !owner_->CanPartFire() || !player_) {
    return;
  }
  const RailCamera *rail = dynamic_cast<const RailCamera *>(camera_);
  const Vector3 myPos = transform_.translate;
  const Vector3 playerPos = player_->GetTransform().translate;

  // 自機より十分前にいて、弾が消えない距離にいて、画面に映り、カメラから見て陰にいないときだけ撃つ
  const Vector3 toPlayer = playerPos - myPos;
  const float forwardFromPlayer = -Dot(toPlayer, baseForward_);
  const bool canShoot = forwardFromPlayer >= kMinFairDistance_ &&
                        Length(toPlayer) <= kMaxShotDistance_ &&
                        IsInScreen() && !player_->IsHiddenFromCamera(myPos);
  if (!canShoot) {
    return;
  }

  fireTimer_ += kFrameTime_;
  if (fireTimer_ < kFireInterval_) {
    return;
  }
  fireTimer_ = 0.0f;

  // 弾が届くころの自機の位置へ撃つ（BehaviorTurret と同じ先読み）
  const Vector3 aim =
      BehaviorTurret::PredictPlayerPos(rail, myPos, playerPos, EnemyBullet::kAimedShotSpeed_) - myPos;
  const float aimLength = Length(aim);
  if (aimLength <= 0.001f) {
    return;
  }
  auto bullet = std::make_unique<EnemyBullet>();
  bullet->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer(), myPos,
                     aim * (EnemyBullet::kAimedShotSpeed_ / aimLength),
                     const_cast<Player *>(player_),
                     EnemyBulletType::NormalDestructible);
  ActorManager::GetInstance()->AddActor(std::move(bullet));
}
