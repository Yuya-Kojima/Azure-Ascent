#include "Spawn/FortressSpawner.h"
#include "Actor/EnemyPart.h"
#include "Debug/Logger.h"
#include "Object3d/Object3d.h"
#include <cmath>
#include <optional>

SpawnResult FortressSpawner::Spawn(const SpawnEvent &ev,
                                   const SpawnContext &context) {
  SpawnResult result;
  auto spline = context.splines->find(ev.splineName);
  if (spline == context.splines->end() || spline->second.size() < 2) {
    Logger::Log("[FortressSpawner] 浮かぶ砦の道筋が見つからない: " +
                ev.splineName + "\n");
    return result;
  }

  // 出し方の moveDirection が 0 でなければ、その向き（ワールド）に固定する
  std::optional<float> heading;
  if (std::abs(ev.moveDirection.x) + std::abs(ev.moveDirection.z) > 1e-6f) {
    heading = std::atan2(ev.moveDirection.x, ev.moveDirection.z);
  }

  auto fortress = std::make_unique<FloatingFortress>();
  fortress->Initialize(context.object3dRenderer, context.camera, spline->second,
                       ev.spawnTime, ev.splineDuration, heading);

  const auto &specs = FloatingFortress::GetPartSpecs();
  for (int i = 0; i < FloatingFortress::kPartCount; ++i) {
    auto part = std::make_unique<EnemyPart>();
    part->Initialize();
    part->SetTag(ActorTag::Enemy);
    auto model = std::make_unique<Object3d>();
    model->Initialize(context.object3dRenderer);
    model->SetModel(kPartModelPath_);
    model->SetSpecularScale(0.0f);
    part->SetModel(std::move(model));
    part->SetBaseColor(kPartColor_);
    part->SetCamera(context.camera);
    part->SetPlayer(context.player);
    part->Setup(fortress.get(), i, specs[i].role, specs[i].localPosition,
                specs[i].halfSize);
    fortress->SetPart(i, part.get());

    // ふだんの敵と同じに、出た敵として数え、自機の弾で倒したときだけ撃破に数える
    context.countForScore(part.get(), ev.spawnTime);
    result.enemies.push_back(std::move(part));
  }
  result.fortress = std::move(fortress);
  return result;
}
