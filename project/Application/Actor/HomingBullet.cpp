#include "HomingBullet.h"
#include "Actor/Enemy.h"
#include "Actor/ILockOnHittable.h"
#include "Camera/RailCamera.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/LineRenderer.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

#include "Collision/CollisionManager.h"
#include "Renderer/TrailRenderer.h"
#include "Render/Camera/ICamera.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Effect/EffectManager.h"

namespace {

// 構図から見た位置（右・上・前の m）をワールドの位置へ直す
Vector3 FramingToWorld(const RailCamera::Framing &frame, const Vector3 &local) {
  return frame.position + frame.right * local.x + frame.up * local.y +
         frame.forward * local.z;
}

// ロックオン弾の着弾・散る演出の色（シアン）
const Vector4 kHomingHitColor = {0.6f, 2.5f, 4.0f, 1.0f};

} // namespace

HomingBullet::HomingBullet() {}
HomingBullet::~HomingBullet() {
  FinishVolley();
  if (onDestroyCallback_) {
    onDestroyCallback_(originalTarget_.Get());
  }
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void HomingBullet::Initialize(Object3dRenderer *renderer,
                              const RailCamera *camera,
                              const Vector3 &localStart,
                              const Vector3 &localBulge, BaseActor *target,
                              float flightSeconds) {
  renderer_ = renderer;
  camera_ = camera;
  localStart_ = localStart;
  localBulge_ = localBulge;
  target_ = target;
  originalTarget_ = target;
  // 0 秒で割らないよう、届く秒は 1 フレーム以上にする
  flightSeconds_ = (std::max)(flightSeconds, kFrameTime_);
  elapsedSeconds_ = 0.0f;

  const RailCamera::Framing &frame = camera_->GetFraming();
  const Vector3 startPos = FramingToWorld(frame, localStart_);
  const Vector3 bulgePos = FramingToWorld(frame, localBulge_);
  if (target) {
    endPoint_ = target->GetTransform().translate;
  } else {
    // 狙う敵がいなければ、P0 から P1 の向きへ同じだけ先へ進んだ所を終わりの点にする
    endPoint_ = bulgePos + (bulgePos - startPos);
  }

  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(renderer);

  object3d_->SetModel("__builtin_capsule");
  // 弾頭モデル：トレイル先端と繋げるためのコア（直径0.5m、長さ4m）
  object3d_->SetScale({0.5f, 4.0f, 0.5f});
  object3d_->SetEnableLighting(false);
  object3d_->SetColor({0.3f, 3.5f, 5.0f, 1.0f}); // シアン（自己発光）
  object3d_->SetTranslation(startPos);
  transform_.translate = startPos;

  // コライダーの設定
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(1.0f);
  collider_->SetAttribute(kCollisionAttributePlayerBullet);
  collider_->SetMask(kCollisionAttributeEnemy | kCollisionAttributeEnemyBullet);
  CollisionManager::GetInstance()->Register(collider_.get());

  // 撃った瞬間は、ふくらみの点へ向ける（ベジェの s=0 の向き）
  FaceDirection(bulgePos - startPos);

  object3d_->Update();
}

void HomingBullet::Update() {
  if (isDead_)
    return;

  // 狙った敵が先に倒れた・消えたら、最後にいた所（endPoint_）をそのまま終わりの点にする
  BaseActor *target = target_.Get();
  if (target && target->IsDead()) {
    target_.Reset();
    target = nullptr;
  }
  // 狙う敵がいなければ、射撃の弾としては終わり
  if (!target) {
    FinishVolley();
  } else {
    endPoint_ = target->GetTransform().translate;
  }

  // 撃ってからの秒を進め、ベジェの上の今の位置へ動かす
  // 届く秒にいちばん近いフレームで s=1 にする
  elapsedSeconds_ += kFrameTime_;
  const bool arrived = elapsedSeconds_ + kFrameTime_ * 0.5f >= flightSeconds_;
  const float s = arrived ? 1.0f : elapsedSeconds_ / flightSeconds_;

  const Vector3 prevPos = object3d_->GetTranslation();
  const Vector3 pos = CalcPosition(s);
  const Vector3 moved = pos - prevPos;
  object3d_->SetTranslation(pos);
  // コライダーの中心座標を同期させる
  transform_.translate = pos;
  if (collider_) {
    collider_->SetVelocity(moved);
  }

  // 弾の向きは、前のフレームから今のフレームへの向き
  FaceDirection(moved);
  object3d_->Update();

  // レーザートレイル履歴の記録（弾頭位置）
  trailHistory_.insert(trailHistory_.begin(), pos);
  if (trailHistory_.size() > kMaxTrailPoints) {
    trailHistory_.pop_back();
  }

  // s=1 で終わりの点に着く
  if (arrived) {
    Arrive();
  }
}

Vector3 HomingBullet::CalcPosition(float s) const {
  // P0・P1 は今の構図の上に置き直す
  const RailCamera::Framing &frame = camera_->GetFraming();
  const Vector3 p0 = FramingToWorld(frame, localStart_);
  const Vector3 p1 = FramingToWorld(frame, localBulge_);

  // 2 次ベジェ: (1-s)^2 P0 + 2(1-s)s P1 + s^2 P2
  const float u = 1.0f - s;
  return p0 * (u * u) + p1 * (2.0f * u * s) + endPoint_ * (s * s);
}

void HomingBullet::FaceDirection(const Vector3 &direction) {
  float lenSq = direction.x * direction.x + direction.y * direction.y +
                direction.z * direction.z;
  if (lenSq <= 0.0001f) {
    return;
  }
  float yaw = std::atan2(direction.x, direction.z);
  float xzLen = std::sqrt(direction.x * direction.x + direction.z * direction.z);
  float pitch = std::atan2(-direction.y, xzLen);
  Vector3 rot = {pitch + (std::numbers::pi_v<float> * 0.5f), yaw, 0.0f};
  object3d_->SetRotation(rot);
  transform_.rotate = rot;
}

void HomingBullet::Arrive() {
  isDead_ = true;

  BaseActor *target = target_.Get();

  // 射撃の弾の終わりを知らせるとその射撃の点が決まるので、先にダメージを与えて倒した数を射撃に足しておく
  auto *hittable = dynamic_cast<ILockOnHittable *>(target);
  if (hittable && !target->IsDead()) {
    hittable->OnLockOnHit(damage_, volleyId_);
    FinishVolley();

    // ホーミング専用着弾演出（太く鋭い十字グリント＋高速プラズマリング）
    EffectManager::GetInstance()->PlayEffect(
        EffectType::HomingHit, object3d_->GetTranslation(), kHomingHitColor);

    Logger::Log("Homing Bullet Hit Target!\n");
    return;
  }

  // 狙った敵がいないときは、終わりの点で小さく散る
  FinishVolley();
  EffectManager::GetInstance()->PlayEffect(
      EffectType::HitSpark, object3d_->GetTranslation(), kHomingHitColor);
}

void HomingBullet::OnCollision(Collider *other) {
  if (isDead_)
    return;

  BaseActor *owner = other->GetOwner();
  if (!owner)
    return;

  // 狙った敵がいる間は、何にも当たらずにすり抜ける（狙った敵には s=1 で当てる）
  if (target_.Get()) {
    return;
  }

  // 狙った敵を失った弾は、途中でぶつかった敵・敵の弾に当たる
  if (other->GetAttribute() & kCollisionAttributeEnemy) {
    Enemy *enemy = dynamic_cast<Enemy *>(owner);
    if (enemy && !enemy->IsDead()) {
      enemy->SetLastHitVolley(volleyId_);
      enemy->TakeDamage(damage_);
      isDead_ = true;
      FinishVolley();

      // ホーミング専用着弾演出（太く鋭い十字グリント＋高速プラズマリング）
      EffectManager::GetInstance()->PlayEffect(
          EffectType::HomingHit, object3d_->GetTranslation(), kHomingHitColor);

      Logger::Log("Homing Bullet Hit Enemy!\n");
    }
  } else if (other->GetAttribute() & kCollisionAttributeEnemyBullet) {
    isDead_ = true;
    FinishVolley();

    // ホーミング専用着弾演出（迎撃時）
    EffectManager::GetInstance()->PlayEffect(
        EffectType::HomingHit, object3d_->GetTranslation(), kHomingHitColor);

    Logger::Log("Homing Bullet Intercepted EnemyBullet!\n");
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
    if (CollisionManager::GetInstance()->IsDebugDrawEnabled()) {
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
