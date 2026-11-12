#include "LockOn.h"
#include "Audio/SoundManager.h"
#include "Math/MathUtil.h"
#include "Framework/BaseActor.h"
#include "Render/Object3d/Object3d.h"
#include <cmath>
#include <algorithm>

void LockOn::Initialize(SpriteRenderer *spriteRenderer) {
  // ロックオンマーカー用スプライトを生成・初期化（最大表示数分）
  for (size_t i = 0; i < kMaxDisplayReticles; ++i) {
    reticles_[i] = std::make_unique<Sprite>();
    reticles_[i]->Initialize(spriteRenderer, "resources/lockon_marker.png");
    reticles_[i]->SetAnchorPoint({0.5f, 0.5f}); // 回転・拡大の中心をスプライト中央に設定
    reticles_[i]->SetSize({64.0f, 64.0f});
    reticles_[i]->SetColor({0.3f, 1.2f, 1.8f, 1.0f});
  }

  targetInfos_.clear();
  lockOnDelayTimer_ = 0;
}

void LockOn::Clear() {
  targetInfos_.clear();
  lockOnDelayTimer_ = 0;
}

void LockOn::ClearLocking() {
  targetInfos_.erase(
      std::remove_if(targetInfos_.begin(), targetInfos_.end(),
                     [](const TargetInfo &info) {
                       return info.state == TargetState::Locking;
                     }),
      targetInfos_.end());
  lockOnDelayTimer_ = 0;
}

void LockOn::OnHomingFired() {
  // 発射時：現在捕捉中の敵をすべて追尾中状態へ昇格させる
  for (auto &info : targetInfos_) {
    if (info.state == TargetState::Locking) {
      info.state = TargetState::Tracking;
    }
  }
  lockOnDelayTimer_ = 0;
}

void LockOn::RemoveTrackingTarget(BaseActor *target) {
  if (!target)
    return;

  auto it = std::find_if(
      targetInfos_.begin(), targetInfos_.end(), [target](const TargetInfo &info) {
        return info.actor.Get() == target && info.state == TargetState::Tracking;
      });
  if (it != targetInfos_.end()) {
    targetInfos_.erase(it);
  }
}

std::vector<BaseActor *> LockOn::GetTargets() const {
  std::vector<BaseActor *> targets;
  targets.reserve(targetInfos_.size());
  for (const auto &info : targetInfos_) {
    BaseActor *actor = info.actor.Get();
    if (info.state == TargetState::Locking && actor && !actor->IsDead()) {
      targets.push_back(actor);
    }
  }
  return targets;
}

size_t LockOn::GetLockingCount() const {
  return std::count_if(
      targetInfos_.begin(), targetInfos_.end(),
      [](const TargetInfo &info) { return info.state == TargetState::Locking; });
}

void LockOn::Update(const std::vector<BaseActor *> &inputTargets,
                    const Matrix4x4 &viewProjectionMatrix,
                    const Vector2 &reticlePos,
                    bool isLockOnMode, float lockOnRadius,
                    const Vector3 &eyePosition) {

  viewProjectionMatrix_ = viewProjectionMatrix; // 描画用にキャッシュ

  // 撃破などで消滅した敵をリストから除外
  for (auto it = targetInfos_.begin(); it != targetInfos_.end();) {
    BaseActor *actor = it->actor.Get();
    if (!actor || actor->IsDead()) {
      it = targetInfos_.erase(it);
    } else {
      it->lockTime += 1.0f / 60.0f; // ロックオン・追尾経過時間を加算
      ++it;
    }
  }

  // --------------------------------------------------
  //  ロックオン対象を探す処理
  // --------------------------------------------------

  if (lockOnDelayTimer_ > 0) {
    lockOnDelayTimer_--;
  }

  // 現在準備中（Locking）の敵数を取得
  size_t lockingCount = GetLockingCount();

  // ロックオンモード（長押し中）かつ、1回の上限および総表示上限未満、ディレイ明けの場合のみ追加
  if (isLockOnMode && lockingCount < kMaxLockOnPerVolley &&
      targetInfos_.size() < kMaxDisplayReticles && lockOnDelayTimer_ <= 0) {
    for (BaseActor *target : inputTargets) {
      if (!target || target->IsDead())
        continue;

      // すでに捕捉中、または現在弾が追尾中の敵は除外（二重ロックオン・無駄撃ち防止）
      bool alreadyLocked = false;
      for (const auto &info : targetInfos_) {
        if (info.actor.Get() == target) {
          alreadyLocked = true;
          break;
        }
      }
      if (alreadyLocked)
        continue;

      Vector3 worldPos = target->GetTransform().translate;
      Vector2 screenPos =
          WorldToScreen(worldPos, viewProjectionMatrix, 1280.0f, 720.0f);

      // 照準と敵の画面上の距離を計算
      float dx = screenPos.x - reticlePos.x;
      float dy = screenPos.y - reticlePos.y;
      float dist = std::sqrt(dx * dx + dy * dy);

      // 照準の範囲内でも、箱の陰にいる敵はロックしない
      if (dist <= lockOnRadius && !IsHidden(eyePosition, worldPos)) {
        targetInfos_.push_back({target, 0.0f, TargetState::Locking});

        // 現在ロックオン中の敵数（今回のターゲットを含む）をカウント
        size_t lockCount = 0;
        for (const auto &info : targetInfos_) {
          if (info.state == TargetState::Locking) {
            lockCount++;
          }
        }

        // ロックオン数に応じて音階を上げる（半音1つで周波数は 2^(1/12) 倍）
        // 1体目: ド 1.00, 2体目: レ 約1.12, 3体目: ミ 約1.26 ... 8体目: 高いド 2.00
        size_t scaleIndex = (std::min)(lockCount, kMaxLockOnPerVolley) - 1;
        float semitones = static_cast<float>(kLockOnScaleSemitones[scaleIndex]);
        float pitchRatio = std::pow(2.0f, semitones / kSemitonesPerOctave);
        SoundManager::GetInstance()->PlaySE("lockon", 1.0f, pitchRatio);

        lockOnDelayTimer_ = kLockOnInterval; // ディレイを開始
        break; // 1フレームに1体ずつロックオンする
      }
    }
  }
}

bool LockOn::IsHidden(const Vector3 &from, const Vector3 &to) const {
  if (!occluders_) {
    return false;
  }
  for (const Matrix4x4 &worldToBox : *occluders_) {
    // 線分を箱の中の座標へ移すと、箱は -1〜1 の立方体になる
    const Vector3 a = TransformPoint(from, worldToBox);
    const Vector3 b = TransformPoint(to, worldToBox);
    const float start[3] = {a.x, a.y, a.z};
    const float delta[3] = {b.x - a.x, b.y - a.y, b.z - a.z};

    // 軸ごとに、線分が -1〜1 の幅に入っている範囲（線分上の割合 0〜1）を重ねていく
    float enter = 0.0f;
    float leave = 1.0f;
    for (int axis = 0; axis < 3 && enter <= leave; ++axis) {
      if (std::abs(delta[axis]) < kParallelEpsilon) {
        // この軸に平行な線分は、幅の外にあれば箱に当たらない
        if (std::abs(start[axis]) > 1.0f) {
          leave = -1.0f;
        }
        continue;
      }
      float t1 = (-1.0f - start[axis]) / delta[axis];
      float t2 = (1.0f - start[axis]) / delta[axis];
      if (t1 > t2) {
        std::swap(t1, t2);
      }
      enter = (std::max)(enter, t1);
      leave = (std::min)(leave, t2);
    }
    if (enter <= leave) {
      return true;
    }
  }
  return false;
}

void LockOn::Draw() {
  const float kBaseSize = 60.0f;         // 基本マーカーサイズ
  const float kConvergeDuration = 0.4f; // 外側からの収束時間（秒）

  size_t drawCount = std::min(targetInfos_.size(), reticles_.size());

  for (size_t i = 0; i < drawCount; ++i) {
    BaseActor *target = targetInfos_[i].actor.Get();
    if (!target)
      continue;

    Vector3 targetPos = target->GetTransform().translate;
    Vector2 screenPos =
        WorldToScreen(targetPos, viewProjectionMatrix_, 1280.0f, 720.0f);

    reticles_[i]->SetPosition(screenPos);

    float elapsed = targetInfos_[i].lockTime;
    float currentSize = kBaseSize;
    float alpha = 1.0f;
    float rotation = 0.0f;

    if (targetInfos_[i].state == TargetState::Locking) {
      // --- 捕捉中（発射前）：画面手前から巨大マーカーが飛来し、激突（めり込み反発） ---
      if (elapsed < kConvergeDuration) {
        float t = elapsed / kConvergeDuration;

        if (t < 0.7f) {
          // 前半 (0%〜70%の時間)：15.0倍の超巨大サイズから 0.5倍 まで猛スピードで飛んできて潰れる
          float t1 = t / 0.7f;
          float easeIn = t1 * t1 * t1; // イージングイン（激突直前が最速）
          currentSize = Lerp(kBaseSize * 15.0f, kBaseSize * 0.5f, easeIn);
          alpha = Lerp(0.0f, 1.0f, easeIn);
        } else {
          // 後半 (70%〜100%の時間)：潰れた 0.5倍 から 1.0倍 に反発して戻る
          float t2 = (t - 0.7f) / 0.3f;
          float easeOut = 1.0f - std::pow(1.0f - t2, 2.0f);
          currentSize = Lerp(kBaseSize * 0.5f, kBaseSize * 1.0f, easeOut);
          alpha = 1.0f;
        }
        
        rotation = (1.0f - t) * 3.0f; // 大きく回転しながら飛んでくる
        reticles_[i]->SetColor({0.3f, 1.3f, 2.0f, alpha});
      } else {
        // --- 捕捉中（発射前）：収束完了後のループ（待機） ---
        float loopTime = elapsed - kConvergeDuration;
        rotation = loopTime * 0.8f;
        float pulse = 1.0f + std::sin(loopTime * 8.0f) * 0.04f;
        currentSize = kBaseSize * pulse;
        alpha = 1.0f;
        reticles_[i]->SetColor({0.3f, 1.3f, 2.0f, alpha});
      }
    } else {
      // --- 追尾中（発射後）：オレンジレッドで敵に吸着・追尾中を明示 ---
      float pulse = 1.0f + std::sin(elapsed * 10.0f) * 0.03f;
      currentSize = (kBaseSize * 0.95f) * pulse; // ややシャープなサイズ
      rotation = 0.785398f; // 45度固定（X字クロスロック形態）
      alpha = 0.95f;
      reticles_[i]->SetColor({2.2f, 0.65f, 0.15f, alpha});
    }

    reticles_[i]->SetSize({currentSize, currentSize});
    reticles_[i]->SetRotation(rotation);

    Transform defaultUV;
    defaultUV.scale = {1.0f, 1.0f, 1.0f};
    defaultUV.rotate = {0.0f, 0.0f, 0.0f};
    defaultUV.translate = {0.0f, 0.0f, 0.0f};
    reticles_[i]->Update(defaultUV);

    reticles_[i]->Draw();
  }
}
