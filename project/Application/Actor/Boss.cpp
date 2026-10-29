#include "Actor/Boss.h"
#include "Audio/SoundManager.h"
#include "Actor/BossBit.h"
#include "Actor/BossCore.h"
#include "Actor/BossWeakPoint.h"
#include "Actor/EnemyBullet.h"
#include "Actor/Player.h"
#include "Framework/ActorManager.h" 
#include "Camera/ICamera.h"
#include "Camera/RailCamera.h"
#include "Collision/CollisionConfig.h"
#include "Collision/CollisionManager.h"
#include "Collision/SphereCollider.h"
#include "Debug/Logger.h"
#include "Effect/EffectManager.h"
#include "Framework/ActorManager.h"
#include "Framework/GameManager.h"
#include "Framework/PrefabManager.h"
#include "Math/MathUtil.h"
#include "Render/Object3d/Object3d.h"
#include "Render/Texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <numbers>

Boss::Boss() {}

Boss::~Boss() {}

void Boss::Initialize() {
  Enemy::Initialize();
  if (collider_) {
    collider_->SetRadius(
        0.4f); // コアより判定を小さくして、弾がボス本体に吸われないようにする
  }
  SetHP(500);
  maxHp_ = 500;
  phase_ = BossPhase::Phase1;
  isDead_ = false;

  currentState_ = BossState::Enter;
  stateTimer_ = 0.0f;
  startPos_ = {0.0f, 0.0f, 0.0f};
  targetPos_ = {0.0f, 0.0f, 0.0f};

  aliveTime_ = 0.0f;
  dyingTimer_ = 0.0f;

  dissolveEnabled_ = false;

  // --- コアと装甲の生成 ---
  float bitOffsetRadius = 0.6f;

  // Z軸を手前にずらす。コアは奥、装甲は手前。
  Vector3 coreOffsets[4] = {
      {0.0f, bitOffsetRadius, -0.6f},  // 上
      {0.0f, -bitOffsetRadius, -0.6f}, // 下
      {-bitOffsetRadius, 0.0f, -0.6f}, // 左
      {bitOffsetRadius, 0.0f, -0.6f}   // 右
  };

  Vector3 bitOffsets[4] = {
      {0.0f, bitOffsetRadius, -1.2f},  // 上
      {0.0f, -bitOffsetRadius, -1.2f}, // 下
      {-bitOffsetRadius, 0.0f, -1.2f}, // 左
      {bitOffsetRadius, 0.0f, -1.2f}   // 右
  };

  for (int i = 0; i < 4; ++i) {
    // 1. コアの生成（奥側）
    auto core = std::make_unique<BossCore>();
    auto coreModel = std::make_unique<Object3d>();
    coreModel->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
    coreModel->SetModel("__builtin_sphere");
    coreModel->SetScale({0.4f, 0.4f, 0.4f});
    coreModel->SetColor({1.0f, 0.1f, 0.2f, 1.0f}); // 赤色オーブ
    core->SetModel(std::move(coreModel));

    core->SetBoss(this);
    core->SetOffset(coreOffsets[i]);

    BossCore *corePtr = core.get();
    ActorManager::GetInstance()->AddActor(std::move(core));

    // 2. 装甲の生成（手前側）
    auto bit = std::make_unique<BossBit>();
    auto bitModel = std::make_unique<Object3d>();
    bitModel->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
    bitModel->SetModel("__builtin_box");
    bitModel->SetScale({0.8f, 1.2f, 0.15f}); // 薄型シールド装甲板
    bitModel->SetColor({0.9f, 0.95f, 1.0f, 1.0f}); // 白銀シールド
    bit->SetModel(std::move(bitModel));

    bit->SetBoss(this);
    bit->SetOffset(bitOffsets[i]);
    bit->SetId(i);

    BossBit *bitPtr = bit.get();
    bit->SetOnDestroyedCallback(
        [this, bitPtr](bool) { this->OnBitDestroyed(bitPtr); });

    // コアに装甲をセットする
    corePtr->SetShield(bitPtr);

    activeBits_.push_back(bitPtr);
    ActorManager::GetInstance()->AddActor(std::move(bit));
  }
}

void Boss::OnBitDestroyed(BossBit *bit) {
  auto it = std::find(activeBits_.begin(), activeBits_.end(), bit);
  if (it != activeBits_.end()) {
    activeBits_.erase(it);
  }
}

void Boss::OnWeakPointDestroyed(BossWeakPoint *wp) {
  // 的が破壊されたら、同じIDを持つ装甲（BossBit）にボーナスダメージを与える
  int targetId = wp->GetId();
  for (auto* bit : activeBits_) {
      if (bit->GetId() == targetId) {
          bit->TakeDamage(15);
          break; 
      }
  }

  auto it = std::find(activeWeakPoints_.begin(), activeWeakPoints_.end(), wp);
  if (it != activeWeakPoints_.end()) {
    activeWeakPoints_.erase(it);
  }

  // 突進中・予兆中に全て破壊されたらカウンター成功
  if (activeWeakPoints_.empty() && (currentState_ == BossState::DashTelegraph || currentState_ == BossState::Dash)) {
      currentState_ = BossState::Stagger;
      stateTimer_ = 0.0f;
      SoundManager::GetInstance()->PlaySE("boss_counter");
      
      // 装甲を元に戻す
      for (auto* bit : activeBits_) {
          bit->ResetPosition();
      }
      
      // ヒットストップ・画面揺れなどの派手な演出
      if (camera_) {
          if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
              auto mutableRailCam = static_cast<RailCamera *>(const_cast<ICamera *>(camera_));
              mutableRailCam->Shake(1.5f, 0.5f); // 激しい揺れ
          }
      }
      EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);
  }
}

void Boss::InitializeUI(SpriteRenderer *spriteRenderer) {
  if (!spriteRenderer)
    return;

  dangerUI_ = std::make_unique<Sprite>();
  dangerUI_->Initialize(spriteRenderer, "resources/white1x1.png"); 
  dangerUI_->SetAnchorPoint({0.5f, 0.5f});
  dangerUI_->SetPosition({1280.0f / 2.0f, 720.0f / 2.0f});
  dangerUI_->SetSize({600.0f, 100.0f}); // 大きな警告バー
  dangerUI_->SetColor({1.0f, 0.0f, 0.0f, 0.5f}); // 半透明の赤

  for (size_t i = 0; i < reticleSprites_.size(); ++i) {
    reticleSprites_[i] = std::make_unique<Sprite>();
    reticleSprites_[i]->Initialize(spriteRenderer, "resources/white1x1.png"); // 全て四角ベースにする
    reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
    reticleSprites_[i]->SetAnchorPoint({0.5f, 0.5f});
  }

  isUIInitialized_ = true;
}

void Boss::Update() {
  if (isDead_) {
    return;
  }

  // 被弾時のフラッシュ処理
  if (hitFlashTimer_ > 0) {
    hitFlashTimer_--;
    if (model_) {
      model_->SetColor({1.0f, 0.0f, 0.0f, 1.0f}); // 赤色に点滅
    }
  } else {
    // 状態に応じたカラーの適用
    if (model_) {
      if (currentState_ == BossState::Telegraph) {
        model_->SetColor({1.0f, 0.5f, 0.0f, 1.0f}); // 予兆時はオレンジ
      } else if (currentState_ == BossState::DashTelegraph) {
        model_->SetColor({1.0f, 0.0f, 0.0f, 1.0f}); // 突進予兆時は赤
      } else {
        model_->SetColor(baseColor_);
      }
    }
  }

  aliveTime_ += 1.0f / 60.0f;

  if (currentState_ == BossState::DashTelegraph ||
      currentState_ == BossState::Dash ||
      currentState_ == BossState::Stagger ||
      currentState_ == BossState::DashCooldown) {
    UpdateDashSequence();
  } else {
    if (phase_ == BossPhase::Phase1) {
      UpdatePhase1();
    } else if (phase_ == BossPhase::PhaseTransition) {
      UpdatePhaseTransition();
    } else if (phase_ == BossPhase::Phase2) {
      UpdatePhase2();
    } else if (phase_ == BossPhase::Dying) {
      UpdateDying();
    }
  }

  // 最後にトランスフォーム（モデル・コライダー位置）を更新する
  UpdateTransform();
}

void Boss::UpdatePhase1() {
  stateTimer_ += 1.0f / 60.0f;

  switch (currentState_) {
  case BossState::Enter:
    // カメラの前方（基準位置）に配置
    if (camera_) {
      Vector3 cPos = camera_->GetTranslate();
      Vector3 cRight = camera_->GetRight();
      Vector3 cUp = camera_->GetUp();
      Vector3 cForward = camera_->GetForward();
      if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
        cPos = railCam->GetRailPosition();
        cRight = railCam->GetRailRight();
        cUp = railCam->GetRailUp();
        cForward = railCam->GetRailForward();
      }
      Vector3 center = cPos + cRight * spawnOffset_.x + cUp * spawnOffset_.y +
                       cForward * spawnOffset_.z;
      transform_.translate = center;
      startPos_ = center;
      targetPos_ = center;
      hoverWaypoints_ = {center, center, center,
                         center}; // 初回は同じ位置に留まる
    }
    currentState_ = BossState::Hover;
    stateTimer_ = 0.0f;
    break;

  case BossState::Hover: {
    float duration = 2.5f; // 飛行にかける時間
    float t = std::min(stateTimer_ / duration, 1.0f);

    // SmoothStepによる緩急
    float easeT = t * t * (3.0f - 2.0f * t);

    // スプライン曲線による移動
    transform_.translate =
        CatmullRom(hoverWaypoints_[0], hoverWaypoints_[1], hoverWaypoints_[2],
                   hoverWaypoints_[3], easeT);

    // 常にプレイヤーの方向を向く
    if (player_) {
      Vector3 pPos = player_->GetTransform().translate;
      Vector3 dir = {pPos.x - transform_.translate.x,
                     pPos.y - transform_.translate.y,
                     pPos.z - transform_.translate.z};
      transform_.rotate.y = std::atan2(dir.x, dir.z) + std::numbers::pi_v<float>;
    }

    // 移動完了で予兆ステートへ
    if (stateTimer_ >= duration) {
      currentState_ = BossState::Telegraph;
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Telegraph: {
    // 予兆：小刻みに震える
    float shakeAmount = 0.2f;
    float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    transform_.translate.x = targetPos_.x + rx;
    transform_.translate.y = targetPos_.y + ry;
    transform_.translate.z = targetPos_.z + rz;

    // 予兆パーティクルエフェクトの発生（チャージの進行度 0.0 ~ 1.0 を渡す）
    float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
    Vector3 effectPos = transform_.translate;
    if (player_) {
        effectPos = player_->GetTransform().translate;
    }
    EffectManager::GetInstance()->PlayBossTelegraphEffect(transform_.translate, effectPos, chargeRatio, attackPattern_);

    // 1秒経過でロックオン完了。ミサイルの場合はアニメーション後さらに1秒間（計2.0秒まで）待ってから発射する
    float telegraphDuration = (attackPattern_ == 1) ? 2.0f : 1.0f;
    if (stateTimer_ >= telegraphDuration) {
      // 震えを元に戻す
      transform_.translate = targetPos_;
      currentState_ = BossState::Attack;
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Attack: {
    EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);

    if (player_) {
      Vector3 playerPos = player_->GetTransform().translate;
      Vector3 myPos = transform_.translate;
      Vector3 dir = {playerPos.x - myPos.x, playerPos.y - myPos.y,
                     playerPos.z - myPos.z};
      float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
      if (dist > 0.001f) {
        dir.x /= dist;
        dir.y /= dist;
        dir.z /= dist;

        auto spawnBullet = [&](const Vector3 &vel, EnemyBulletType type,
                               int waitFrames = 0) {
          auto bullet = std::make_unique<EnemyBullet>();
          bullet->Initialize(
              PrefabManager::GetInstance()->GetObject3dRenderer(), myPos, vel,
              const_cast<Player *>(player_), type);
          if (type == EnemyBulletType::LockOnDestructible) {
            bullet->SetSwarmWait(waitFrames);
          }
          ActorManager::GetInstance()->AddActor(std::move(bullet));
        };

        if (attackPattern_ == 1) {
          // ミサイル
          for (int i = 0; i < 4; ++i) {
            Vector3 rightDir = {dir.z, 0.0f, -dir.x};
            Vector3 backwardDir = {-dir.x, 0.0f, -dir.z};
            float bLen = std::sqrt(backwardDir.x * backwardDir.x +
                                   backwardDir.z * backwardDir.z);
            if (bLen > 0.001f) {
              backwardDir.x /= bLen;
              backwardDir.z /= bLen;
            }

            float spread = (i == 0)   ? -1.0f
                           : (i == 1) ? -0.4f
                           : (i == 2) ? 0.4f
                                      : 1.0f;
            float upwardForce = ((rand() % 100) / 100.0f) * 0.5f;
            float launchSpeed =
                15.0f; // 横への展開速度を少しだけ落とす（18.0 -> 15.0）
            float backwardSpeed =
                0.0f; // 後ろには飛ばさず、ボスの真横に展開する
            int waitFrames =
                40 + (i * 50); // 一発ごとの間隔を 30f から 50f に延長
            Vector3 vel = {rightDir.x * spread * launchSpeed +
                               backwardDir.x * backwardSpeed,
                           upwardForce,
                           rightDir.z * spread * launchSpeed +
                               backwardDir.z * backwardSpeed};
            spawnBullet(vel, EnemyBulletType::LockOnDestructible, waitFrames);
          }
        } else {
          // 通常弾（扇状）
          float normalSpeed = 0.5f;
          for (int i = 0; i < 5; ++i) {
            float angle = (-20.0f + i * 10.0f) * (std::numbers::pi_v<float> / 180.0f);
            Vector3 vel = {
                (dir.x * std::cos(angle) + dir.z * std::sin(angle)) *
                    normalSpeed,
                dir.y * normalSpeed,
                (-dir.x * std::sin(angle) + dir.z * std::cos(angle)) *
                    normalSpeed};
            spawnBullet(vel, EnemyBulletType::NormalDestructible);
          }
        }

        if (camera_) {
          if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
            static_cast<RailCamera *>(const_cast<ICamera *>(camera_))
                ->Shake(0.3f, 0.1f);
          }
        }
      }
    }

    currentState_ = BossState::Cooldown;
    stateTimer_ = 0.0f;
    break;
  }

  case BossState::Cooldown: {
    // 2秒経過したら再びHoverに戻ってループ
    if (stateTimer_ >= 2.0f) {
      startPos_ = transform_.translate;

      // 次の攻撃パターンを切り替える（ランダム抽選）
      // 0:通常弾, 1:ミサイル, 2:突進
      std::vector<int> availablePatterns = {0, 1};
      
      if (dashCooldown_ > 0) {
          dashCooldown_--;
      } else {
          // クールダウンが明けていれば突進(2)を抽選候補に加える
          availablePatterns.push_back(2);
      }
      
      int randomIndex = rand() % availablePatterns.size();
      attackPattern_ = availablePatterns[randomIndex];
      
      if (attackPattern_ == 2) {
          dashCooldown_ = 3; // 突進を選んだら3ターンのクールダウンを設定
          currentState_ = BossState::DashTelegraph;
          stateTimer_ = 0.0f;
          return; // Hoverへは行かずに突進予兆へ遷移
      }

      if (camera_) {
        Vector3 cPos = camera_->GetTranslate();
        Vector3 cRight = camera_->GetRight();
        Vector3 cUp = camera_->GetUp();
        Vector3 cForward = camera_->GetForward();
        if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
          cPos = railCam->GetRailPosition();
          cRight = railCam->GetRailRight();
          cUp = railCam->GetRailUp();
          cForward = railCam->GetRailForward();
        }

        // 1. 攻撃パターンに基づく目標座標（着地地点）の決定 = 予兆（Telegraph）
        if (attackPattern_ == 1) {
          // ミサイル：画面の奥深く（遠距離）へ
          targetPos_ = cPos + cUp * 15.0f + cForward * 250.0f;
        } else {
          // 通常弾：プレイヤーの近く（近距離）へ
          targetPos_ = cPos + cUp * 5.0f + cForward * 110.0f;
        }

        // 少しランダムにズラす
        float offsetX =
            ((rand() % 100) / 100.0f - 0.5f) * 40.0f; // ズレ幅も少し抑える
        targetPos_.x += offsetX;

        // 2. スプライン曲線の制御点を生成（行き方はランダム）
        hoverWaypoints_[1] = startPos_;
        hoverWaypoints_[2] = targetPos_;

        // 引っ張り点（P0, P3）をランダムに配置して有機的なカーブを作る
        int flightPattern = rand() % 3;
        if (flightPattern == 0) {
          // 左から大きく回り込む
          hoverWaypoints_[0] = cPos - cRight * 600.0f + cUp * 50.0f;
          hoverWaypoints_[3] = cPos + cRight * 600.0f - cUp * 50.0f;
        } else if (flightPattern == 1) {
          // 上空へ大きく膨らむ
          hoverWaypoints_[0] = cPos + cUp * 600.0f + cForward * 100.0f;
          hoverWaypoints_[3] = cPos - cUp * 600.0f + cForward * 100.0f;
        } else {
          // 右から大きく回り込む
          hoverWaypoints_[0] = cPos + cRight * 600.0f + cUp * 50.0f;
          hoverWaypoints_[3] = cPos - cRight * 600.0f - cUp * 50.0f;
        }
      } else {
        targetPos_ = startPos_;
        hoverWaypoints_ = {startPos_, startPos_, startPos_, startPos_};
      }

      currentState_ = BossState::Hover;
      stateTimer_ = 0.0f;
    }
    break;
  }
  }
}

void Boss::UpdatePhaseTransition() {
  stateTimer_ += 1.0f / 60.0f;
  float duration = 3.5f;

  // 1. ボスの退避移動 
  float moveDuration = duration - 0.5f; // 最後の0.5秒はバースト用
  float moveT = std::min(stateTimer_ / moveDuration, 1.0f);
  float easeT = moveT * moveT * (3.0f - 2.0f * moveT);

  transform_.translate.x = std::lerp(startPos_.x, targetPos_.x, easeT);
  transform_.translate.y = std::lerp(startPos_.y, targetPos_.y, easeT);
  transform_.translate.z = std::lerp(startPos_.z, targetPos_.z, easeT);

  // 2. ボスのシェイクとカラー点滅
  float shakePower = 1.0f * (stateTimer_ / duration);
  float rx = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  float ry = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  float rz = (static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f) * shakePower;
  transform_.translate.x += rx;
  transform_.translate.y += ry;
  transform_.translate.z += rz;

  float flashSpeed = 10.0f + 30.0f * (stateTimer_ / duration);
  float flashIntensity = (std::sin(stateTimer_ * flashSpeed) + 1.0f) * 0.5f;
  if (model_) {
      Vector4 flashColor = {1.0f, 1.0f - flashIntensity * 0.8f, 1.0f - flashIntensity * 0.8f, 1.0f};
      model_->SetColor(flashColor);
  }

  // 3. カメラのズームとシェイク
  if (camera_) {
      if (auto railCam = const_cast<RailCamera*>(dynamic_cast<const RailCamera *>(camera_))) {
          float defaultFov = 45.0f * (3.14159265f / 180.0f);
          float zoomFov = 25.0f * (3.14159265f / 180.0f); // 寄る
          
          float fovT = std::min(stateTimer_ / 1.0f, 1.0f); // 最初の1秒でズームしてそのまま維持
          float easeFovT = fovT * fovT * (3.0f - 2.0f * fovT);
          float currentFov = std::lerp(defaultFov, zoomFov, easeFovT);
          railCam->SetFov(currentFov);
          
          // ボスへの注視設定
          railCam->SetFocusTarget(&transform_.translate);
          railCam->SetFocusWeight(easeFovT); // FOVと同じ緩急で注視を強める
          
          railCam->Shake(0.5f * (stateTimer_ / duration), 0.1f);
      }
  }

  // 4. 移行完了でPhase2へ
  if (stateTimer_ >= duration) {
      if (model_) {
          model_->SetColor(baseColor_);
      }
      if (camera_) {
          if (auto railCam = const_cast<RailCamera*>(dynamic_cast<const RailCamera *>(camera_))) {
              // ここではFOVもフォーカスも戻さない（Phase2側で引き戻す）
              railCam->Shake(3.0f, 0.5f);
          }
      }
      EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);
      ChangePhase(BossPhase::Phase2);
  }
}

void Boss::UpdatePhase2() {
  stateTimer_ += 1.0f / 60.0f;

  // 形態変化直後のカメラ引き戻し演出
  if (attackStep_ == -1 && currentState_ == BossState::Cooldown && camera_) {
      if (auto railCam = const_cast<RailCamera*>(dynamic_cast<const RailCamera *>(camera_))) {
          float defaultFov = 45.0f * (3.14159265f / 180.0f);
          float zoomFov = 25.0f * (3.14159265f / 180.0f);
          
          // 最初の1.0秒間はズームを維持（爆発の余韻と変化後の姿を見せる）
          // その後の1.0秒間（stateTimer_ が 1.0 ～ 2.0）でズームを元に戻す
          float pullBackT = 0.0f;
          if (stateTimer_ > 1.0f) {
              pullBackT = std::min((stateTimer_ - 1.0f) / 1.0f, 1.0f);
          }
          float easePullBack = 1.0f - std::pow(1.0f - pullBackT, 3.0f); // イーズアウト
          
          float currentFov = std::lerp(zoomFov, defaultFov, easePullBack);
          railCam->SetFov(currentFov);
          
          // 注視（フォーカス）も、カメラ引き戻しと同じタイミングでスッと解除する
          railCam->SetFocusTarget(&transform_.translate);
          railCam->SetFocusWeight(1.0f - easePullBack); 
          
          // 引き戻しが完了したら完全に注視を解除
          if (stateTimer_ >= 2.0f) {
              railCam->SetFocusTarget(nullptr);
              railCam->SetFocusWeight(0.0f);
          }
      }
  }

  switch (currentState_) {
  case BossState::Enter:
  case BossState::Hover: {
    // 高速・広範囲なイージング移動
    float t = std::min(stateTimer_ / 1.5f, 1.0f);
    float easeT = 1.0f - std::pow(1.0f - t, 3.0f);

    transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeT;
    transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeT;
    transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeT;

    if (player_) {
      Vector3 pPos = player_->GetTransform().translate;
      Vector3 dir = {pPos.x - transform_.translate.x,
                     pPos.y - transform_.translate.y,
                     pPos.z - transform_.translate.z};
      transform_.rotate.y = std::atan2(dir.x, dir.z) + std::numbers::pi_v<float>;
    }

    if (stateTimer_ >= 1.5f) {
      if (attackPattern_ == 1) {
        currentState_ = BossState::Telegraph; // ミサイル攻撃へ
      } else if (attackPattern_ == 2) {
        currentState_ = BossState::DashTelegraph; // 突進攻撃へ
      }
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Telegraph: {
    // 激しい予兆（ミサイル用）
    float shakeAmount = 0.4f;
    float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
    transform_.translate.x = targetPos_.x + rx;
    transform_.translate.y = targetPos_.y + ry;
    transform_.translate.z = targetPos_.z + rz;

    // 予兆パーティクルエフェクトの発生（チャージの進行度 0.0 ~ 1.0 を渡す）
    float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
    Vector3 effectPos = transform_.translate;
    if (player_) {
        effectPos = player_->GetTransform().translate;
    }
    EffectManager::GetInstance()->PlayBossTelegraphEffect(transform_.translate, effectPos, chargeRatio, 1); // Phase 2のTelegraphはミサイル固定なので1

    if (stateTimer_ >= 1.0f) {
      transform_.translate = targetPos_;
      currentState_ = BossState::Attack;
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Attack: {
    // 発射の瞬間に発散エフェクト（大爆発＋ショックウェーブ）を発生
    EffectManager::GetInstance()->PlayBossBurstEffect(transform_.translate);

    if (player_) {
      Vector3 playerPos = player_->GetTransform().translate;
      Vector3 myPos = transform_.translate;
      Vector3 dir = {playerPos.x - myPos.x, playerPos.y - myPos.y,
                     playerPos.z - myPos.z};
      float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
      if (dist > 0.001f) {
        dir.x /= dist;
        dir.y /= dist;
        dir.z /= dist;
        auto spawnBullet = [&](const Vector3 &vel, EnemyBulletType type,
                               int waitFrames) {
          auto bullet = std::make_unique<EnemyBullet>();
          bullet->Initialize(
              PrefabManager::GetInstance()->GetObject3dRenderer(), myPos, vel,
              const_cast<Player *>(player_), type);
          bullet->SetSwarmWait(
              waitFrames); // ランダムではなく指定された待機時間をセット
          ActorManager::GetInstance()->AddActor(std::move(bullet));
        };

        // スウォーム弾幕（6発）をボスの周囲に円形展開（12時方向から時計回りに順次発射）
        constexpr int kMissileCount = 6;
        float deploySpeedX = 7.5f; 
        float deploySpeedY = 3.8f; 
        Vector3 rightDir = {dir.z, 0.0f, -dir.x};
        Vector3 upDir = {0.0f, 1.0f, 0.0f};
        if (camera_) {
          upDir = camera_->GetUp();
        }

        Vector3 backwardDir = {-dir.x, 0.0f, -dir.z};
        float bLen = std::sqrt(backwardDir.x * backwardDir.x +
                               backwardDir.z * backwardDir.z);
        if (bLen > 0.001f) {
          backwardDir.x /= bLen;
          backwardDir.z /= bLen;
        }

        float backwardSpeed = 6.0f; // 本来の奥方向（ボスの背後）への展開速度

        for (int i = 0; i < kMissileCount; ++i) {
          // 12時方向（真上）を基準に、時計回りに60度ずつ配置
          // i=0: 上, i=1: 右上, i=2: 右下, i=3: 真下, i=4: 左下, i=5: 左上
          float angle = static_cast<float>(i) * (2.0f * std::numbers::pi_v<float> / static_cast<float>(kMissileCount));
          float sinAngle = std::sin(angle);
          float cosAngle = std::cos(angle);

          // 12時から時計回りに順番に発射
          int waitFrames = 80 + (i * 20);

          Vector3 vel = {
              rightDir.x * (sinAngle * deploySpeedX) + upDir.x * (cosAngle * deploySpeedY) + backwardDir.x * backwardSpeed,
              rightDir.y * (sinAngle * deploySpeedX) + upDir.y * (cosAngle * deploySpeedY) + backwardDir.y * backwardSpeed,
              rightDir.z * (sinAngle * deploySpeedX) + upDir.z * (cosAngle * deploySpeedY) + backwardDir.z * backwardSpeed};

          spawnBullet(vel, EnemyBulletType::LockOnDestructible, waitFrames);
        }

        if (camera_) {
          if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
            auto mutableRailCam =
                static_cast<RailCamera *>(const_cast<ICamera *>(camera_));
            mutableRailCam->Shake(0.5f, 0.15f);
          }
        }
      }
    }
    currentState_ = BossState::Cooldown;
    stateTimer_ = 0.0f;
    break;
  }

  case BossState::Cooldown: {
    float waitTime = (attackStep_ == -1) ? 2.0f : 1.0f; // 形態変化直後は演出用に長め（2.0秒）に待つ
    if (stateTimer_ >= waitTime) {
      attackStep_++;
      
      // ランダム抽選ロジック
      std::vector<int> availablePatterns = {1}; // 1: ミサイル
      if (dashCooldown_ > 0) {
          dashCooldown_--;
      } else {
          availablePatterns.push_back(2); // 2: 突進
      }
      int randomIndex = rand() % availablePatterns.size();
      attackPattern_ = availablePatterns[randomIndex];

      if (attackPattern_ == 2) {
          dashCooldown_ = 3;
      }
      
      startPos_ = targetPos_;
      if (camera_) {
        Vector3 cPos = camera_->GetTranslate();
        Vector3 cRight = camera_->GetRight();
        Vector3 cUp = camera_->GetUp();
        Vector3 cForward = camera_->GetForward();
        if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
          cPos = railCam->GetRailPosition();
          cRight = railCam->GetRailRight();
          cUp = railCam->GetRailUp();
          cForward = railCam->GetRailForward();
        }
        // 次の攻撃パターンに応じて目標位置を変える
        if (attackPattern_ == 1) {
          // 次はミサイル攻撃：画面奥深く（突進と同じ距離200）へ移動
          targetPos_ = cPos + cForward * 200.0f;
          float offsetX = ((rand() % 100) / 100.0f - 0.5f) * 40.0f;
          targetPos_.x += offsetX;
        } else if (attackPattern_ == 2) {
          // 次は突進攻撃：一旦手前（基準位置付近）へ移動し、その後大きく下がる演出に繋げる
          Vector3 center = cPos + cRight * spawnOffset_.x + cUp * spawnOffset_.y +
                           cForward * spawnOffset_.z;
          float offsetX = ((rand() % 100) / 100.0f - 0.5f) * 24.0f;
          float offsetY = ((rand() % 100) / 100.0f - 0.5f) * 12.0f;
          targetPos_.x = center.x + offsetX;
          targetPos_.y = center.y + offsetY;
          targetPos_.z = center.z;
        }
      }
      currentState_ = BossState::Hover;
      stateTimer_ = 0.0f;
    }
    break;
  }


  }
}

void Boss::UpdateDying() {
  dyingTimer_ += 1.0f / 60.0f;

  // ディゾルブ進行
  if (model_) {
    if (!dissolveEnabled_ && dyingTimer_ > 0.0f) {
      model_->SetEnableDissolve(true);
      dissolveEnabled_ = true;
    }
    float progress = dyingTimer_ / dyingDuration_;
    progress = std::min(progress, 1.0f);
    model_->SetDissolveThreshold(progress);
  }

  // 塵パーティクルの位置更新
  float progressForDust = dyingTimer_ / dyingDuration_;
  if (onDyingUpdateCallback_ && progressForDust < 0.6f) {
    onDyingUpdateCallback_(transform_.translate);
  }

  // 演出終了 → 完全消滅
  if (dyingTimer_ >= dyingDuration_) {
    ChangePhase(BossPhase::Defeated);
  }
}

void Boss::Draw3D() {
  if (model_) {
    model_->Draw();
  }
}

void Boss::Draw2D() {
  if (!isUIInitialized_ || isDead_)
    return;

  Transform defaultUv;
  defaultUv.scale = {1.0f, 1.0f, 1.0f};
  defaultUv.rotate = {0.0f, 0.0f, 0.0f};
  defaultUv.translate = {0.0f, 0.0f, 0.0f};

  // 突進予兆時の警告UI描画
  if (currentState_ == BossState::DashTelegraph) {
      // 点滅させる
      float alpha = (std::sin(stateTimer_ * 20.0f) + 1.0f) * 0.5f * 0.8f;
      dangerUI_->SetColor({1.0f, 0.0f, 0.0f, alpha});
      dangerUI_->Update(defaultUv);
      dangerUI_->Draw();
  }

  // ミサイル予兆中のレティクル描画 (Phase2の場合は常にミサイル)
  if (currentState_ == BossState::Telegraph && (attackPattern_ == 1 || phase_ == BossPhase::Phase2) && player_ && camera_) {
    float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
    
    Vector3 playerPos = player_->GetTransform().translate;
    Vector2 screenPos = WorldToScreen(playerPos, camera_->GetViewProjectionMatrix(), 1280.0f, 720.0f);

    // 画面外を描画から除外
    if (screenPos.x > -100 && screenPos.x < 1380 && screenPos.y > -100 && screenPos.y < 820) {
      
      // チャージ比率 (0.0~1.0)
      float chargeRatio = std::min(stateTimer_ / 1.0f, 1.0f);
      bool isLocked = stateTimer_ >= 1.0f; // 1.0秒以降はロック完了状態

      Transform uvTransform;
      uvTransform.scale = {1.0f, 1.0f, 1.0f};
      uvTransform.rotate = {0.0f, 0.0f, 0.0f};
      uvTransform.translate = {0.0f, 0.0f, 0.0f};
      
      //  外枠 
      // 外枠は縮小せず、画面中央を中心に公転・自転する。ターゲットを囲む四角い枠（□）にする
      float outerOffset = 160.0f;
      float outerThick = 6.0f;
      float outerLen = 100.0f;
      // 1.0秒でピッタリ90度回転し、四角い枠のままカチッと止まる
      float outerRot = stateTimer_ * (std::numbers::pi_v<float> / 2.0f);
      float finalOuterRot = isLocked ? (std::numbers::pi_v<float> / 2.0f) : outerRot;

      constexpr float baseAngles[4] = {
          0.0f,                               // 上
          std::numbers::pi_v<float>,          // 下
          -std::numbers::pi_v<float> / 2.0f,  // 左
          std::numbers::pi_v<float> / 2.0f    // 右
      };

      for (int i=0; i<4; ++i) {
         float currentAngle = baseAngles[i] + finalOuterRot;
         float ox = sinf(currentAngle) * outerOffset;
         float oy = -cosf(currentAngle) * outerOffset;
         
         reticleSprites_[i]->SetPosition({screenPos.x + ox, screenPos.y + oy});
         // 外枠は「横線（-）」をベースにして回転させることで四角い枠（□）を形成する
         reticleSprites_[i]->SetSize({outerLen, outerThick}); 
         reticleSprites_[i]->SetRotation(currentAngle);
         
         reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, isLocked ? 1.0f : 0.6f});
         reticleSprites_[i]->Update(uvTransform);
         reticleSprites_[i]->Draw();
      }

      //内枠
      // チャージが進むにつれて枠が激しく縮小する。内枠はターゲットを狙う十字（＋）にする
      float innerOffset = 200.0f - (150.0f * chargeRatio);
      float innerThick = 10.0f;
      float innerLen = 50.0f;
      // 1.0秒でピッタリ180度回転する
      float innerRot = -stateTimer_ * std::numbers::pi_v<float>;
      float finalInnerRot = isLocked ? -std::numbers::pi_v<float> : innerRot;

      float innerAlpha = 1.0f;
      if (!isLocked && chargeRatio > 0.7f) {
         innerAlpha = fmodf(stateTimer_ * 15.0f, 1.0f) > 0.5f ? 1.0f : 0.2f;
      }
      
      for (int i=4; i<8; ++i) {
         int idx = i - 4;
         float currentAngle = baseAngles[idx] + finalInnerRot;
         float ox = sinf(currentAngle) * innerOffset;
         float oy = -cosf(currentAngle) * innerOffset;
         
         reticleSprites_[i]->SetPosition({screenPos.x + ox, screenPos.y + oy});
         // 内枠は「縦線（|）」をベースにして回転させることで十字（＋）を形成する
         reticleSprites_[i]->SetSize({innerThick, innerLen}); 
         reticleSprites_[i]->SetRotation(currentAngle);
         
         reticleSprites_[i]->SetColor({1.0f, 0.0f, 0.0f, innerAlpha});
         reticleSprites_[i]->Update(uvTransform);
         reticleSprites_[i]->Draw();
      }

      // センタードット
      if (isLocked) {
         reticleSprites_[8]->SetPosition({screenPos.x, screenPos.y});
         reticleSprites_[8]->SetSize({14.0f, 14.0f}); 
         reticleSprites_[8]->SetRotation(0.0f);
         
         reticleSprites_[8]->SetScale({1.0f, 1.0f});
         reticleSprites_[8]->SetColor({1.0f, 0.0f, 0.0f, 1.0f});
         reticleSprites_[8]->Update(uvTransform);
         reticleSprites_[8]->Draw();
      }
    }
  }
}

void Boss::OnCollision(Collider *other) {
  if (other->GetOwner() && dynamic_cast<Player *>(other->GetOwner())) {
    Player *p = dynamic_cast<Player *>(other->GetOwner());
    if (p->GetInvincibleTimer() > 0) {
      return; // 無敵中は食らわない
    }
    p->TakeDamage(1); // プレイヤーにダメージを与える
  }
}

void Boss::TakeDamage(int damage, bool isSelfDestruct) {
  // 形態変化中、死亡演出中、または死亡済みの場合はダメージを受け付けない（演出リセット防止）
  if (isDead_ || phase_ == BossPhase::PhaseTransition || phase_ == BossPhase::Dying || phase_ == BossPhase::Defeated)
    return;

  // 装甲が1つでも残っている間はボス本体へのダメージを完全に無効化する
  if (!activeBits_.empty())
    return;

  hp_ -= damage;
  hitFlashTimer_ = 5;

  // フェーズ移行判定 (T-2)
  if (hp_ <= 0) {
    hp_ = 0;
    ChangePhase(BossPhase::Dying); // 即消滅ではなくDyingフェーズへ
  } else if (phase_ == BossPhase::Phase1 && hp_ <= maxHp_ * 0.7f) {
    ChangePhase(BossPhase::PhaseTransition);
  }
}

void Boss::ChangePhase(BossPhase nextPhase) {
  phase_ = nextPhase;
  if (phase_ == BossPhase::PhaseTransition) {
    Logger::Log("Boss entering Phase Transition!\n");
    
    // 形態変化に入った瞬間に、画面上のすべての敵弾（EnemyBullet）を消去する
    ActorManager::GetInstance()->ClearActorsIf([](BaseActor* actor) {
        return dynamic_cast<EnemyBullet*>(actor) != nullptr;
    });
    
    stateTimer_ = 0.0f;
    startPos_ = transform_.translate;
    if (camera_) {
      Vector3 cPos = camera_->GetTranslate();
      Vector3 cUp = camera_->GetUp();
      Vector3 cForward = camera_->GetForward();
      if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
        cPos = railCam->GetRailPosition();
        cUp = railCam->GetRailUp();
        cForward = railCam->GetRailForward();
      }
      // 画面奥深くへ退避
      targetPos_ = cPos + cForward * 200.0f;
    } else {
      targetPos_ = startPos_;
    }
  } else if (phase_ == BossPhase::Phase2) {
    Logger::Log("Boss entering Phase 2!\n");
    // パターン変化の初期化など
    currentState_ = BossState::Cooldown; // 形態変化直後はスキを作る（カメラ引き戻し用）
    stateTimer_ = 0.0f;
    attackStep_ = -1; // -1から始めることで、次の行動がミサイル（0 % 2 == 0）になる
    startPos_ = transform_.translate;
    targetPos_ = transform_.translate;
  } else if (phase_ == BossPhase::Dying) {
    Logger::Log("Boss entering Dying phase!\n");
    
    // ボス撃破と同時に画面上のすべての敵弾（ミサイル含む）を誘爆させる
    ActorManager::GetInstance()->ClearActorsIf([](BaseActor* actor) {
        if (auto* bullet = dynamic_cast<EnemyBullet*>(actor)) {
            bullet->Explode();
            return true; // 誘爆させつつリストからも直ちに消去する
        }
        // 残存しているすべてのコア（BossCore）も爆発エフェクトと共に消去する
        if (auto* core = dynamic_cast<class BossCore*>(actor)) {
            EffectManager::GetInstance()->PlayEnemyDeathSimpleEffect(core->GetTransform().translate, {1.0f, 0.5f, 0.0f, 1.0f});
            return true; 
        }
        return false;
    });

    // コライダーを無効化（メモリは破棄しない）
    if (collider_) {
      collider_->SetEnable(false);
    }
    dyingTimer_ = 0.0f;
    // HPバーを非表示にする
    isUIInitialized_ = false;
  } else if (phase_ == BossPhase::Defeated) {
    Logger::Log("Boss Defeated!\n");
    onDyingUpdateCallback_ = nullptr; // 塵の放出を止める

    if (onDestroyedCallback_) {
      onDestroyedCallback_(false);
    }
    isDead_ = true;
  }
}

void Boss::UpdateDashSequence() {
  stateTimer_ += 1.0f / 60.0f;
  switch (currentState_) {
  case BossState::DashTelegraph: {
    // 初回のみ：装甲退避と的のスポーン
    if (stateTimer_ <= 0.02f) {
        SoundManager::GetInstance()->PlaySE("boss_charge");
        for (auto* bit : activeBits_) {
            bit->SpreadOut();
        }
        Vector3 wpOffsets[4] = {
            { 0.0f,  1.8f, -1.0f},
            { 0.0f, -1.8f, -1.0f},
            {-1.8f,  0.0f, -1.0f},
            { 1.8f,  0.0f, -1.0f}
        };
        for (int i = 0; i < 4; ++i) {
            auto wp = std::make_unique<BossWeakPoint>();
            auto model = std::make_unique<Object3d>();
            model->Initialize(PrefabManager::GetInstance()->GetObject3dRenderer());
            model->SetModel("__builtin_crystal");
            model->SetColor({1.0f, 0.2f, 0.2f, 1.0f});
            wp->SetModel(std::move(model));
            wp->SetBoss(this);
            wp->SetOffset(wpOffsets[i]);
            wp->SetId(i);
            wp->GetTransform().scale = {3.0f, 3.0f, 3.0f};
            
            BossWeakPoint* wpPtr = wp.get();
            wp->SetOnDestroyedCallback([this, wpPtr](bool) { this->OnWeakPointDestroyed(wpPtr); });
            
            activeWeakPoints_.push_back(wpPtr);
            ActorManager::GetInstance()->AddActor(std::move(wp));
        }
        
        // Z奥へ下がるための目標位置を設定
        startPos_ = transform_.translate;
        if (camera_) {
            Vector3 cPos = camera_->GetTranslate();
            Vector3 cForward = camera_->GetForward();
            if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
                cPos = railCam->GetRailPosition();
                cForward = railCam->GetRailForward();
            }
            targetPos_ = cPos + cForward * 200.0f; // かなり奥へ
        }
    }

    // 最初の0.5秒で奥へ下がる
    float moveT = std::min(stateTimer_ / 0.5f, 1.0f);
    float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f);
    transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeMoveT;
    transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeMoveT;
    transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeMoveT;

    // 奥に到達してから大きく震える
    if (moveT >= 1.0f) {
        float shakeAmount = 0.6f;
        float rx = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
        float ry = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
        float rz = ((rand() % 100) / 100.0f - 0.5f) * shakeAmount;
        transform_.translate.x = targetPos_.x + rx;
        transform_.translate.y = targetPos_.y + ry;
        transform_.translate.z = targetPos_.z + rz;
    }

    // 突進予兆パーティクルエフェクトの発生
    float totalTelegraphTime = 8.0f;

    if (stateTimer_ >= totalTelegraphTime) {
      transform_.translate = targetPos_;
      startPos_ = targetPos_;
      
      // 突進の目標座標を「プレイヤーの現在の位置のさらに後方」へ設定
      if (player_ && camera_) {
          Vector3 pPos = player_->GetTransform().translate;
          Vector3 cForward = camera_->GetForward();
          if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
              cForward = railCam->GetRailForward();
          }
          targetPos_ = pPos - cForward * 50.0f; // 自機の背面へ通り抜ける
      } else if (camera_) {
          Vector3 cPos = camera_->GetTranslate();
          Vector3 cForward = camera_->GetForward();
          if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
              cPos = railCam->GetRailPosition();
              cForward = railCam->GetRailForward();
          }
          targetPos_ = cPos - cForward * 50.0f; // カメラの背面へ通り抜ける
      } else {
          targetPos_.z -= 200.0f;
      }

      if (camera_) {
        if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
          auto mutableRailCam = static_cast<RailCamera *>(const_cast<ICamera *>(camera_));
          mutableRailCam->Shake(0.8f, 0.3f); // 突進開始時の大きな揺れ
        }
      }

      currentState_ = BossState::Dash;
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Dash: {
    // 0.8秒で画面を通り抜ける超高速移動
    float t = std::min(stateTimer_ / 0.8f, 1.0f);
    float easeT = t * t; // EaseIn

    transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeT;
    transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeT;
    transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeT;

    // 途中でプレイヤーとの衝突判定は OnCollision で行われる。
    // (Player側で被弾処理が行われる)

    if (stateTimer_ >= 0.8f) {
      // 突進終了（カウンター失敗で通り抜けた後）
      // 残った的を安全に消す
      for (auto* wp : activeWeakPoints_) {
          wp->SetOnDestroyedCallback(nullptr); // コールバックを解除
          wp->Destroy(); // 即死させる
      }
      activeWeakPoints_.clear();
      // 装甲を元に戻す
      for (auto* bit : activeBits_) {
          bit->ResetPosition();
      }

      // カメラ後方から再配置し、Cooldownへ
      currentState_ = BossState::DashCooldown;
      stateTimer_ = 0.0f;
    }
    break;
  }

  case BossState::Stagger: {
      // カウンター成功時、大きく後方へ弾き飛ばされる
      if (stateTimer_ <= 0.02f) {
          startPos_ = transform_.translate;
          if (camera_) {
              Vector3 cForward = camera_->GetForward();
              if (auto railCam = dynamic_cast<const RailCamera *>(camera_)) {
                  cForward = railCam->GetRailForward();
              }
              // 奥へ吹っ飛ぶ目標 + 少し下に落ちる
              targetPos_ = startPos_ + cForward * 100.0f;
              targetPos_.y -= 25.0f;
          }
      }

      // ヒットストップのため最初の0.3秒間は止まる
      if (stateTimer_ < 0.3f) {
          // ボス自身も激しく振動させる（ブレ）
          float shake = 0.2f;
          transform_.rotate.z += ((rand() % 100) / 100.0f - 0.5f) * shake;
          transform_.rotate.x += ((rand() % 100) / 100.0f - 0.5f) * shake;
      } else if (stateTimer_ < 1.3f) {
          // 0.3〜1.3秒で、奥・下へノックバック
          float moveT = (stateTimer_ - 0.3f) / 1.0f;
          float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f); // EaseOut
          transform_.translate.x = startPos_.x + (targetPos_.x - startPos_.x) * easeMoveT;
          transform_.translate.y = startPos_.y + (targetPos_.y - startPos_.y) * easeMoveT;
          transform_.translate.z = startPos_.z + (targetPos_.z - startPos_.z) * easeMoveT;
      } else {
          // 1.3〜2.0秒で、元の位置(startPos_)へ復帰
          float moveT = (stateTimer_ - 1.3f) / 0.7f;
          float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f); // EaseOut
          transform_.translate.x = targetPos_.x + (startPos_.x - targetPos_.x) * easeMoveT;
          transform_.translate.y = targetPos_.y + (startPos_.y - targetPos_.y) * easeMoveT;
          transform_.translate.z = targetPos_.z + (startPos_.z - targetPos_.z) * easeMoveT;
      }

      // 姿勢を崩す（仰け反り＋横に傾く）
      // 0.3秒〜1.3秒で最大まで傾き、1.3秒〜2.0秒で元に戻る
      float tiltT = 0.0f;
      if (stateTimer_ < 1.3f) {
          tiltT = (stateTimer_ - 0.3f) / 1.0f; 
          if (tiltT < 0.0f) tiltT = 0.0f;
          tiltT = std::sin(tiltT * 3.141592f / 2.0f); // EaseOut
      } else {
          // 姿勢もEaseOutで戻るように移動と同じ計算を使用する
          float moveT = (stateTimer_ - 1.3f) / 0.7f;
          float easeMoveT = 1.0f - std::pow(1.0f - moveT, 3.0f);
          tiltT = 1.0f - easeMoveT;
          if (tiltT < 0.0f) tiltT = 0.0f;
      }
          
      // 後ろへ仰け反り（X回転）と、力なく横に倒れる（Z回転）
      transform_.rotate.x = tiltT * -1.2f; // 上を向くように仰け反る
      transform_.rotate.z = tiltT * 0.8f;  // 横に傾く

      if (stateTimer_ >= 2.0f) {
          currentState_ = BossState::Cooldown;
          stateTimer_ = 0.0f;
      }
      break;
  }

  case BossState::DashCooldown: {
    // 突進後、1.5秒間隙を晒す
    if (stateTimer_ >= 1.5f) {
      currentState_ = BossState::Cooldown;
      stateTimer_ = 0.0f;
    }
    break;
  }
  }
}
