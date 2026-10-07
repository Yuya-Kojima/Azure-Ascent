#include "SceneFactory.h"
#include "DebugScene.h"
#include "FractureTestScene.h"
#include "GamePlayScene.h"
#include "TitleScene.h"
#include "StageSelectScene.h"

std::unique_ptr<BaseScene>
SceneFactory::CreateScene(const std::string &sceneName) {

  // 次のシーンを生成
  BaseScene *newScene = nullptr;

  if (sceneName == "TITLE") {
    return std::make_unique<TitleScene>();
  } else if (sceneName == "GAMEPLAY") {
    return std::make_unique<GamePlayScene>();
  } else if (sceneName == "STAGE_SELECT") {
    return std::make_unique<StageSelectScene>();
  } else if (sceneName == "DEBUG") {
    return std::make_unique<DebugScene>();
  } else if (sceneName == "FRACTURE_TEST") {
    return std::make_unique<FractureTestScene>();
  }

  assert(false && "Unknown sceneName");
  return nullptr;
}
