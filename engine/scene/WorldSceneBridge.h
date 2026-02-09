#pragma once

#include "scene/EntityID.h"
#include "scene/NyxScene.h"

#include <string>
#include <unordered_map>

namespace Nyx {

class World;

struct WorldToSceneResult final {
  NyxScene scene{};
  std::unordered_map<EntityID, SceneEntityID, EntityHash> worldToScene;
};

struct SceneToWorldResult final {
  std::unordered_map<SceneEntityID, EntityID> sceneToWorld;
};

class WorldSceneBridge final {
public:
  static WorldToSceneResult exportWorld(const World &w,
                                        const std::string &sceneName);

  static SceneToWorldResult importScene(World &w, const NyxScene &scene,
                                        bool clearWorldFirst = true);
};

} // namespace Nyx
