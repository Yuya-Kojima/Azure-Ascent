#include "Spawn/EnemySpawner.h"
#include "Camera/RailCamera.h"

Vector3 EnemySpawner::CalcSpawnPosition(const SpawnEvent &ev,
                                        const RailCamera &camera) {
  if (ev.isWorldSpace) {
    return ev.spawnOffset;
  }
  return camera.GetRailPosition() + camera.GetRailRight() * ev.spawnOffset.x +
         camera.GetRailUp() * ev.spawnOffset.y +
         camera.GetRailForward() * ev.spawnOffset.z;
}
