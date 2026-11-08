#include "EnemyBullet.h"
#include "Actor/HomingBullet.h"
#include "Actor/NormalBullet.h"
#include "Actor/Player.h"
#include "Collision/CollisionConfig.h"
#include "Collision/CollisionManager.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Effect/EffectManager.h"
#include "Math/MathUtil.h"
#include "Render/Camera/ICamera.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Renderer/Object3dRenderer.h"
#include "Renderer/TrailRenderer.h"
#include <cmath>
#include <numbers>

EnemyBullet::EnemyBullet() {}
EnemyBullet::~EnemyBullet() {
  if (collider_) {
    CollisionManager::GetInstance()->Remove(collider_.get());
  }
}

void EnemyBullet::Initialize(Object3dRenderer *renderer,
                             const Vector3 &startPos, const Vector3 &velocity,
                             Player *player, EnemyBulletType type) {
  renderer_ = renderer;
  object3d_ = std::make_unique<Object3d>();
  object3d_->Initialize(renderer);

  type_ = type;
  velocity_ = velocity;
  player_ = player;
  lifeTimer_ = 1800; // 約30秒で消滅（安全装置としての寿命）

  // 自己発光モード（周囲の光に影響されず美しく発光）
  object3d_->SetEnableLighting(false);

  float colliderRadius = 0.8f;

  // タイプごとの見た目とHPの設定
  if (type_ == EnemyBulletType::NormalDestructible) {
    // 通常ショット破壊可能弾（黄色）
    object3d_->SetModel("__builtin_sphere");
    object3d_->SetScale({1.8f, 1.8f, 1.8f});
    object3d_->SetColor({4.0f, 1.8f, 0.2f, 1.0f});
    colliderRadius = 1.4f;
    hp_ = 1;
    SetTag(ActorTag::ShotTarget); // 通常ショットの狙いの助けの対象としてタグ付け
  } else if (type_ == EnemyBulletType::LockOnDestructible) {
    // 親オブジェクト（姿勢・移動）：メッシュ描画は行わない
    object3d_->SetScale({1.0f, 1.0f, 1.0f});

    // 子オブジェクト（結晶メッシュ）：進行軸周りの自転を担当
    crystalObject_ = std::make_unique<Object3d>();
    crystalObject_->Initialize(renderer);
    crystalObject_->SetModel("__builtin_crystal");
    crystalObject_->SetScale({2.6f, 2.6f, 3.8f}); // 視認性確保のための大型結晶サイズ
    crystalObject_->SetColor({0.8f, 3.5f, 6.0f, 1.0f}); // シアンブルー（自己発光）
    crystalObject_->SetParent(object3d_.get());

    colliderRadius = 2.2f;
    hp_ = 1;
    SetTag(ActorTag::LockOnTarget); // ロックオン対象としてタグ付け
  } else {
    // 破壊不可弾（青紫）
    object3d_->SetModel("__builtin_sphere");
    object3d_->SetScale({3.5f, 3.5f, 3.5f});
    object3d_->SetColor({2.0f, 0.6f, 6.0f, 1.0f});
    colliderRadius = 2.8f;
    hp_ = 9999;
  }

  object3d_->SetTranslation(startPos);

  // 初速ベクトルから初期回転を計算（+Z軸が進行方向）
  float lenSq = velocity_.x * velocity_.x + velocity_.y * velocity_.y + velocity_.z * velocity_.z;
  if (lenSq > 0.0001f) {
    float yaw = std::atan2(velocity_.x, velocity_.z);
    float xzLen = std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    float pitch = std::atan2(-velocity_.y, xzLen);
    Vector3 rot = {pitch, yaw, 0.0f};
    object3d_->SetRotation(rot);
    transform_.rotate = rot;
  }

  // 初回フレームの描画前にワールド行列を即時確定させる
  object3d_->Update();
  if (crystalObject_) {
    crystalObject_->Update();
  }

  // コライダーの設定
  collider_ = std::make_unique<SphereCollider>(this);
  collider_->SetRadius(colliderRadius);
  collider_->SetAttribute(kCollisionAttributeEnemyBullet);
  // プレイヤー自身と、プレイヤーの弾の両方と衝突判定を行う
  collider_->SetMask(kCollisionAttributePlayer |
                     kCollisionAttributePlayerBullet);
  collider_->SetVelocity(velocity_);
  CollisionManager::GetInstance()->Register(collider_.get());
}

void EnemyBullet::Update() {
  if (isDead_)
    return;

  lifeTimer_--;
  if (lifeTimer_ <= 0) {
    isDead_ = true;
  }

  // 描画限界距離（カリング距離）による消滅判定
  if (player_ && !player_->IsDead()) {
    Vector3 currentPos = object3d_->GetTranslation();
    Vector3 playerPos = player_->GetTransform().translate;
    Vector3 toPlayer = {playerPos.x - currentPos.x, playerPos.y - currentPos.y,
                        playerPos.z - currentPos.z};
    float distSq = toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y +
                   toPlayer.z * toPlayer.z;
    if (distSq > 300.0f * 300.0f) { // プレイヤーから距離300以上離れたら消滅
      isDead_ = true;
      return;
    }
  }

  // 位置の更新の前に、ミサイル特有の誘導処理を入れる
  aliveFrames_++;
  if (type_ == EnemyBulletType::LockOnDestructible) {
    if (player_ && !player_->IsDead()) {
      Vector3 currentPos = object3d_->GetTranslation();
      Vector3 playerPos = player_->GetTransform().translate;

      Vector3 toPlayer = {playerPos.x - currentPos.x,
                          playerPos.y - currentPos.y,
                          playerPos.z - currentPos.z};
      float dist = std::sqrt(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y +
                             toPlayer.z * toPlayer.z);

      if (dist > 0.001f) {
        toPlayer.x /= dist;
        toPlayer.y /= dist;
        toPlayer.z /= dist;
      }

      if (aliveFrames_ < swarmWaitFrames_) {
        int framesLeft = swarmWaitFrames_ - aliveFrames_;
        if (framesLeft == 1) {
          // タメから解放され、発射されるまさにその瞬間
          // カメラ空間のRightとUpを取得して、画面の視野枠（端）に向けて散開させる
          Vector3 camRight = {1.0f, 0.0f, 0.0f};
          Vector3 camUp = {0.0f, 1.0f, 0.0f};
          if (renderer_ && renderer_->GetDefaultCamera()) {
            Matrix4x4 viewMat = renderer_->GetDefaultCamera()->GetViewMatrix();
            Matrix4x4 camWorld = Inverse(viewMat);
            camRight = {camWorld.m[0][0], camWorld.m[0][1], camWorld.m[0][2]};
            camUp = {camWorld.m[1][0], camWorld.m[1][1], camWorld.m[1][2]};
          }

          // 自機から見てミサイルが左右どちら側にいるかを判定
          Vector3 diff = {currentPos.x - playerPos.x, currentPos.y - playerPos.y, currentPos.z - playerPos.z};
          float sideSign = (Dot(diff, camRight) >= 0.0f) ? 1.0f : -1.0f;

          // プレイヤー方向へまっすぐ発射し、画面外への飛び出しを完全に防止
          Vector3 spreadDir = toPlayer;

          float burstSpeed = 2.0f;
          velocity_ = {spreadDir.x * burstSpeed, spreadDir.y * burstSpeed,
                       spreadDir.z * burstSpeed};

          // 旋回開始の初期値を設定
          homingStrength_ = 0.02f;

          // 発射時の衝撃波リングエフェクト（真っ白）を発生
          EffectManager::GetInstance()->PlayEffect(
              EffectType::MuzzleRing, object3d_->GetTranslation(),
              {1.0f, 1.0f, 1.0f, 1.0f});
        } else if (framesLeft < 15) {
          // 発射直前の約0.25秒は完全に静止してタメを作る
          velocity_ = {0.0f, 0.0f, 0.0f};
        } else {
          // 展開中は急ブレーキをかける
          velocity_.x *= 0.85f;
          velocity_.y *= 0.85f;
          velocity_.z *= 0.85f;
          // ほんの少しだけ重力で落としてホバリング感を出す
          velocity_.y -= 0.01f;
        }
      } else {
        // 現在の進行方向と、プレイヤーへの方向の内積を計算
        Vector3 vNorm = velocity_;
        float vLen = std::sqrt(vNorm.x * vNorm.x + vNorm.y * vNorm.y +
                               vNorm.z * vNorm.z);
        if (vLen > 0.001f) {
          vNorm.x /= vLen;
          vNorm.y /= vLen;
          vNorm.z /= vLen;
        }
        float dot =
            vNorm.x * toPlayer.x + vNorm.y * toPlayer.y + vNorm.z * toPlayer.z;

        // プレイヤーに十分近づいた（距離40未満）かつすれ違ったなら誘導終了
        if (dist < 40.0f && dot < 0.0f) {
          homingStrength_ = -1.0f; // 負の数を入れて誘導終了フラグとする
        }

        // homingStrength_ が 0 以上なら誘導を続ける（-1ならそのまま直進）
        if (homingStrength_ >= 0.0f) {
          homingStrength_ += 0.008f; // 画面外に逃げずしっかり自機へ向く
          if (homingStrength_ > 0.12f)
            homingStrength_ = 0.12f;

          float missileSpeed = 1.2f;
          Vector3 desiredVelocity = {toPlayer.x * missileSpeed,
                                     toPlayer.y * missileSpeed,
                                     toPlayer.z * missileSpeed};

          velocity_.x = Lerp(velocity_.x, desiredVelocity.x, homingStrength_);
          velocity_.y = Lerp(velocity_.y, desiredVelocity.y, homingStrength_);
          velocity_.z = Lerp(velocity_.z, desiredVelocity.z, homingStrength_);
        }
      }
    }
  }

  // 位置の更新
  Vector3 pos = object3d_->GetTranslation();
  pos.x += velocity_.x;
  pos.y += velocity_.y;
  pos.z += velocity_.z;
  object3d_->SetTranslation(pos);
  transform_.translate = pos;

  // コライダーの更新
  if (collider_) {
    collider_->SetVelocity(velocity_);
  }

  // 弾の向き（回転）の計算
  Vector3 forwardDir = velocity_;

  // ミサイルのタメ・展開ホバリング中は、速度ではなくプレイヤーの方向を向いて狙いを定める
  if (type_ == EnemyBulletType::LockOnDestructible && aliveFrames_ < swarmWaitFrames_) {
    if (player_ && !player_->IsDead()) {
      Vector3 playerPos = player_->GetTransform().translate;
      forwardDir = {playerPos.x - pos.x, playerPos.y - pos.y, playerPos.z - pos.z};
    }
  }

  // 弾の向き（forwardDir）へ親オブジェクトを向ける（Pitch, Yawのみ）
  float lenSq = forwardDir.x * forwardDir.x + forwardDir.y * forwardDir.y + forwardDir.z * forwardDir.z;
  if (lenSq > 0.0001f) {
    float yaw = std::atan2(forwardDir.x, forwardDir.z);
    float xzLen = std::sqrt(forwardDir.x * forwardDir.x + forwardDir.z * forwardDir.z);
    float pitch = std::atan2(-forwardDir.y, xzLen);

    Vector3 rot = {pitch, yaw, 0.0f};
    object3d_->SetRotation(rot);
    transform_.rotate = rot;
  }

  // 親（姿勢・位置）のワールド行列を確定
  object3d_->Update();

  // 誘導弾の場合、子オブジェクトを親の進行軸（ローカルZ軸）周りに自転（Roll）させる
  if (crystalObject_) {
    rollAngle_ += 0.2f;
    if (rollAngle_ > std::numbers::pi_v<float> * 2.0f) {
      rollAngle_ -= std::numbers::pi_v<float> * 2.0f;
    }
    crystalObject_->SetRotation({0.0f, 0.0f, rollAngle_});
    crystalObject_->Update();
  }

  // 誘導弾の発射後（待機終了後）、後端からトレイル履歴を追加
  if (type_ == EnemyBulletType::LockOnDestructible && aliveFrames_ >= swarmWaitFrames_ && crystalObject_) {
    // 結晶の後端（ローカル Z = -1.0）のワールド座標を取得
    const Matrix4x4& world = crystalObject_->GetWorldMatrix();
    Vector3 crystalTailPos = {
        world.m[3][0] - world.m[2][0] * 1.0f,
        world.m[3][1] - world.m[2][1] * 1.0f,
        world.m[3][2] - world.m[2][2] * 1.0f
    };

    trailHistory_.insert(trailHistory_.begin(), crystalTailPos);
    if (trailHistory_.size() > kMaxTrailPoints) {
      trailHistory_.pop_back();
    }
  }
}

void EnemyBullet::OnCollision(Collider *other) {
  if (isDead_)
    return;

  // 相手がプレイヤーの場合
  if (other->GetOwner() && dynamic_cast<Player *>(other->GetOwner())) {
    Player *p = dynamic_cast<Player *>(other->GetOwner());
    if (p->GetInvincibleTimer() > 0) {
      return; // 無敵中はすり抜ける（消えない）
    }
    isDead_ = true;
    p->TakeDamage(1);
    return;
  }

  // 破壊不可弾は何が当たっても壊れない
  if (type_ == EnemyBulletType::Indestructible)
    return;

  // 相手がプレイヤーの弾かチェック
  if (other->GetAttribute() & kCollisionAttributePlayerBullet) {
    BaseActor *bulletOwner = other->GetOwner();

    // 相手が通常ショット（NormalBullet）の場合
    if (dynamic_cast<NormalBullet *>(bulletOwner)) {
      if (type_ == EnemyBulletType::NormalDestructible ||
          type_ == EnemyBulletType::LockOnDestructible) {
        hp_ -= 1; // 通常ショットで1ダメージ
        Logger::Log("EnemyBullet: Hit by NormalBullet!\n");
      }
    }
    // 相手がロックオンレーザー（HomingBullet）の場合
    else if (auto *homing = dynamic_cast<HomingBullet *>(bulletOwner)) {
      // そのホーミング弾が自分（this）を狙っている場合のみ被弾
      if (homing->GetTarget() == this) {
        if (type_ == EnemyBulletType::NormalDestructible ||
            type_ == EnemyBulletType::LockOnDestructible) {
          hp_ -= 3; // ロックオンレーザーで3ダメージ（誘導弾を一撃破壊）
          Logger::Log("EnemyBullet: Hit by targeted HomingBullet!\n");
        }
      }
    }

    if (hp_ <= 0) {
      Logger::Log("EnemyBullet: Intercepted!\n");
      Explode(); // エフェクトを発生させて自身を破棄
    }
  }
}

void EnemyBullet::Explode() {
    if (isDead_) return;
    isDead_ = true;
    
    // タイプに応じた色で爆発エフェクトを発生させる
    Vector4 color = {1.0f, 0.5f, 0.0f, 1.0f}; // 通常弾：オレンジ
    if (type_ == EnemyBulletType::LockOnDestructible) {
        color = {0.2f, 0.8f, 1.0f, 1.0f}; // 誘導弾：シアンブルー
    } else if (type_ == EnemyBulletType::Indestructible) {
        color = {0.0f, 0.0f, 1.0f, 1.0f}; // 破壊不可弾：青
    }
    
    EffectManager::GetInstance()->PlayEnemyDeathSimpleEffect(object3d_->GetTranslation(), color);
}

void EnemyBullet::Draw3D() {
  if (!isDead_) {
    if (crystalObject_) {
      crystalObject_->Draw();
    } else if (object3d_) {
      object3d_->Draw();
    }
  }

  // 誘導弾のリボントレイル描画登録
  if (type_ == EnemyBulletType::LockOnDestructible && trailHistory_.size() >= 2) {
    std::vector<TrailRenderer::TrailNode> nodes;
    nodes.reserve(trailHistory_.size());

    size_t count = trailHistory_.size();
    for (size_t i = 0; i < count; ++i) {
      float t = static_cast<float>(i) / static_cast<float>(count - 1); // 0.0(弾頭直後) 〜 1.0(末尾)
      // 太さ：弾頭幅（2.6m）から末尾（0.3m）へ補間
      float width = Lerp(2.6f, 0.3f, t);
      // アルファ：末尾に向かって線形フェードアウト
      float alpha = 1.0f - t;
      // シアンブルーの光帯
      Vector4 color = {0.6f * alpha, 2.8f * alpha, 5.5f * alpha, alpha};

      nodes.push_back({trailHistory_[i], width, color});
    }

    // カメラ座標の取得
    Vector3 cameraPos = {0.0f, 0.0f, 0.0f};
    if (renderer_ && renderer_->GetDefaultCamera()) {
      cameraPos = renderer_->GetDefaultCamera()->GetTranslate();
    } else if (player_) {
      cameraPos = player_->GetTransform().translate;
    }

    TrailRenderer::GetInstance()->AddTrail(nodes, cameraPos);
  }
}
