#pragma once
#include "SceneRuntime.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Nyx {

class World;
class MaterialSystem;
class NyxProjectRuntime;

class SceneManager final {
public:
  void init(World &world, MaterialSystem &materials, NyxProjectRuntime &project);
  void shutdown();

  bool openScene(const std::string &absPath);
  bool createScene(const std::string &absPath);
  bool saveActive();
  bool saveActiveAs(const std::string &absPath);
  bool saveAllProjectScenes();

  bool hasActive() const { return m_active.has_value(); }
  SceneRuntime &active() { return *m_active; }
  const SceneRuntime &active() const { return *m_active; }
  uint64_t sceneChangeSerial() const { return m_sceneChangeSerial; }

  const std::vector<std::string> &projectScenes() const;

private:
  World *m_world = nullptr;
  MaterialSystem *m_materials = nullptr;
  NyxProjectRuntime *m_project = nullptr;
  mutable std::vector<std::string> m_scenePathsCache;

  std::optional<SceneRuntime> m_active;
  uint64_t m_sceneChangeSerial = 0;

  void ensureSceneListed(const std::string &relPath);
};

} // namespace Nyx
