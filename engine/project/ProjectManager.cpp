#include "ProjectManager.h"
#include "../app/EngineContext.h"
#include "scene/World.h"
#include "serialization/SceneSerializer.h"

#include <algorithm>
#include <filesystem>

namespace Nyx {

namespace fs = std::filesystem;

void ProjectManager::init(EngineContext &engine, std::string absEditorCfgPath) {
  m_engine = &engine;
  m_editorCfgAbs = std::move(absEditorCfgPath);
  loadEditorConfig();
}

void ProjectManager::shutdown() {
  saveEditorConfig();
  m_engine = nullptr;
}

void ProjectManager::loadEditorConfig() {
  if (m_editorCfgAbs.empty())
    return;
  auto cfg = EditorUserConfigIO::load(m_editorCfgAbs);
  if (cfg)
    m_userCfg = std::move(*cfg);

  // Sanitize stale/corrupt entries and canonicalize to absolute paths.
  const std::vector<std::string> before = m_userCfg.recent.items;
  std::vector<std::string> fixed;
  fixed.reserve(before.size());
  for (const std::string &raw : before) {
    if (raw.empty())
      continue;
    if (raw.find('\0') != std::string::npos)
      continue;
    bool badControl = false;
    for (unsigned char c : raw) {
      if (c < 32 && c != '\t') {
        badControl = true;
        break;
      }
    }
    if (badControl)
      continue;

    std::filesystem::path p(raw);
    if (!p.is_absolute())
      p = std::filesystem::absolute(p);
    p = p.lexically_normal();

    if (p.extension() != ".nyxproj")
      continue;

    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
      continue;

    const std::string abs = p.string();
    if (std::find(fixed.begin(), fixed.end(), abs) == fixed.end())
      fixed.push_back(abs);
  }
  m_userCfg.recent.items = std::move(fixed);
  if (m_userCfg.recent.items != before)
    saveEditorConfig();
}

void ProjectManager::saveEditorConfig() {
  if (m_editorCfgAbs.empty())
    return;
  (void)EditorUserConfigIO::save(m_editorCfgAbs, m_userCfg);
}

bool ProjectManager::makeDefaultFolders(const std::string &projectDirAbs,
                                        const std::string &assetRootRel) {
  try {
    fs::path root(projectDirAbs);
    fs::path content = root / fs::path(assetRootRel);
    fs::create_directories(content);
    fs::create_directories(content / "Scenes");
    fs::create_directories(content / "Textures");
    fs::create_directories(content / "Materials");
    fs::create_directories(content / "Meshes");
    fs::create_directories(content / "Environments");
    fs::create_directories(root / "Intermediate"); // future cooking
    fs::create_directories(root / "Saved");        // editor/runtime data
    return true;
  } catch (...) {
    return false;
  }
}

bool ProjectManager::openProject(const std::string &absNyxprojPath) {
  std::filesystem::path p(absNyxprojPath);
  if (!p.is_absolute())
    p = std::filesystem::absolute(p);
  p = p.lexically_normal();
  const std::string abs = p.string();

  if (!m_proj.openProject(abs))
    return false;

  addRecent(abs);

  // Optional: auto-open startup scene if set
  // (You can wire this to your SceneIO/.nyxscene loader later.)
  // const auto& startup = m_proj.proj().settings.startupScene;

  return true;
}

void ProjectManager::addRecent(const std::string &absNyxproj) {
  std::filesystem::path p(absNyxproj);
  if (!p.is_absolute())
    p = std::filesystem::absolute(p);
  p = p.lexically_normal();
  m_userCfg.recent.add(p.string());
  saveEditorConfig();
}

bool ProjectManager::createProject(const std::string &absNyxprojPath,
                                   const std::string &projectName,
                                   bool createDefaultFolders) {
  std::filesystem::path projPath(absNyxprojPath);
  if (!projPath.is_absolute())
    projPath = std::filesystem::absolute(projPath);
  projPath = projPath.lexically_normal();
  const std::string projAbs = projPath.string();

  static constexpr const char *kMainSceneRel = "Content/Scenes/Main.nyxscene";

  NyxProject p{};
  p.name = projectName.empty() ? "NyxProject" : projectName;
  p.assetRootRel = "Content";
  p.settings.exposure = 1.0f;
  p.settings.vsync = true;
  p.settings.startupScene = kMainSceneRel;
  p.scenes.clear();
  p.scenes.push_back({kMainSceneRel, "Main"});

  // Ensure folders
  const std::string dir = NyxProjIO::dirname(projAbs);
  if (createDefaultFolders) {
    if (!makeDefaultFolders(dir, p.assetRootRel))
      return false;
  }

  // Create default scene on project creation.
  {
    const std::string mainSceneAbs = NyxProjIO::joinPath(dir, kMainSceneRel);
    std::error_code ec;
    const fs::path scenePath(mainSceneAbs);
    if (scenePath.has_parent_path())
      fs::create_directories(scenePath.parent_path(), ec);

    World freshWorld{};
    if (!SceneSerializer::save(mainSceneAbs, freshWorld))
      return false;
  }

  if (!NyxProjIO::save(projAbs, p))
    return false;

  return openProject(projAbs);
}

bool ProjectManager::createProjectAt(const std::string &projectRootAbs,
                                     const std::string &name) {
  std::filesystem::path root(projectRootAbs);
  if (!root.is_absolute())
    root = std::filesystem::absolute(root);
  root = root.lexically_normal();
  if (root.empty())
    return false;

  std::error_code ec;
  std::filesystem::create_directories(root, ec);
  if (ec)
    return false;

  std::string projectName = name.empty() ? "NyxProject" : name;
  std::filesystem::path projPath = root / (projectName + ".nyxproj");
  return createProject(projPath.string(), projectName, true);
}

} // namespace Nyx
