#include "SceneManager.h"
#include "scene/World.h"
#include "project/NyxProjectRuntime.h"
#include "render/material/MaterialSystem.h"
#include "serialization/SceneSerializer.h"

#include <algorithm>
#include <filesystem>

namespace Nyx {

void SceneManager::init(World &world, MaterialSystem &materials,
                        NyxProjectRuntime &project) {
  m_world = &world;
  m_materials = &materials;
  m_project = &project;
}

void SceneManager::shutdown() { m_active.reset(); }

const std::vector<std::string> &SceneManager::projectScenes() const {
  m_scenePathsCache.clear();
  if (!m_project || !m_project->hasProject())
    return m_scenePathsCache;

  m_scenePathsCache.reserve(m_project->proj().scenes.size());
  for (const auto &entry : m_project->proj().scenes)
    m_scenePathsCache.push_back(entry.relPath);
  return m_scenePathsCache;
}

void SceneManager::ensureSceneListed(const std::string &relPath) {
  if (!m_project || !m_project->hasProject() || relPath.empty())
    return;

  auto &scenes = m_project->proj().scenes;
  const auto it =
      std::find_if(scenes.begin(), scenes.end(),
                   [&](const NyxProjectSceneEntry &e) { return e.relPath == relPath; });
  if (it != scenes.end())
    return;

  NyxProjectSceneEntry e{};
  e.relPath = relPath;
  e.name = std::filesystem::path(relPath).stem().string();
  scenes.push_back(std::move(e));
}

bool SceneManager::openScene(const std::string &absPath) {
  if (!m_world || !m_materials)
    return false;

  m_materials->reset();
  if (!SceneSerializer::load(absPath, *m_world))
    return false;

  SceneRuntime rt{};
  rt.pathAbs = absPath;
  rt.pathRel = m_project ? m_project->makeRelative(absPath) : absPath;
  rt.dirty = false;

  ensureSceneListed(rt.pathRel);
  m_active = std::move(rt);
  ++m_sceneChangeSerial;
  return true;
}

bool SceneManager::createScene(const std::string &absPath) {
  if (!m_world)
    return false;

  std::error_code ec;
  const std::filesystem::path p(absPath);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);

  m_world->clear();
  if (!SceneSerializer::save(absPath, *m_world))
    return false;

  SceneRuntime rt{};
  rt.pathAbs = absPath;
  rt.pathRel = m_project ? m_project->makeRelative(absPath) : absPath;
  rt.dirty = false;

  ensureSceneListed(rt.pathRel);
  m_active = std::move(rt);
  ++m_sceneChangeSerial;
  return true;
}

bool SceneManager::saveActive() {
  if (!m_active || !m_world)
    return false;
  std::error_code ec;
  const std::filesystem::path p(m_active->pathAbs);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);
  if (!SceneSerializer::save(m_active->pathAbs, *m_world))
    return false;

  m_active->dirty = false;
  return true;
}

bool SceneManager::saveActiveAs(const std::string &absPath) {
  if (!m_world)
    return false;
  std::error_code ec;
  const std::filesystem::path p(absPath);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);
  if (!SceneSerializer::save(absPath, *m_world))
    return false;

  SceneRuntime rt{};
  rt.pathAbs = absPath;
  rt.pathRel = m_project ? m_project->makeRelative(absPath) : absPath;
  rt.dirty = false;
  ensureSceneListed(rt.pathRel);
  m_active = std::move(rt);
  return true;
}

bool SceneManager::saveAllProjectScenes() {
  if (!m_project || !m_project->hasProject())
    return false;

  bool any = false;
  for (const auto &entry : m_project->proj().scenes) {
    const std::string abs = m_project->makeAbsolute(entry.relPath);
    if (abs.empty())
      continue;

    if (m_active && m_active->pathAbs == abs) {
      if (!saveActive())
        return false;
      any = true;
      continue;
    }

    // Best-effort normalize/refresh existing scenes to the current serializer.
    World tmp{};
    if (!SceneSerializer::load(abs, tmp))
      continue;
    if (!SceneSerializer::save(abs, tmp))
      return false;
    any = true;
  }

  return any;
}

} // namespace Nyx
