#include "Spawn/SerpentSpawner.h"
#include "Actor/EnemyPart.h"
#include "Debug/Logger.h"
#include "Object3d/Object3d.h"

SpawnResult SerpentSpawner::Spawn(const SpawnEvent &ev,
                                  const SpawnContext &context) {
  SpawnResult result;
  auto spline = context.splines->find(ev.splineName);
  if (spline == context.splines->end() || spline->second.size() < 2) {
    Logger::Log("[SerpentSpawner] 大蛇の道筋が見つからない: " + ev.splineName +
                "\n");
    return result;
  }

  auto serpent = std::make_unique<MechSerpent>();
  serpent->Initialize(context.object3dRenderer, context.camera, context.player,
                      context.interceptCue, spline->second, ev.spawnTime,
                      ev.splineDuration, ev.serpent);

  const auto &specs = serpent->GetPartSpecs();
  for (int i = 0; i < static_cast<int>(specs.size()); ++i) {
    auto part = std::make_unique<EnemyPart>();
    part->Initialize();
    part->SetTag(ActorTag::Enemy);
    auto model = std::make_unique<Object3d>();
    model->Initialize(context.object3dRenderer);
    model->SetModel(kPartModelPath_);
    model->SetSpecularScale(0.0f);
    part->SetModel(std::move(model));
    part->SetBaseColor(specs[i].color);
    part->SetCamera(context.camera);
    part->SetPlayer(context.player);
    part->Setup(serpent.get(), i, specs[i].role, specs[i].localPosition,
                specs[i].halfSize);
    serpent->SetPart(i, part.get());

    // ふだんの敵と同じに、出た敵として数え、自機の弾で倒したときだけ撃破に数える
    context.countForScore(part.get(), ev.spawnTime);
    result.enemies.push_back(std::move(part));
  }
  result.serpent = std::move(serpent);
  return result;
}
