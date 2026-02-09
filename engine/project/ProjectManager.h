#pragma once
#include "EditorUserConfig.h"
#include "NyxProjectRuntime.h"

#include <string>
#include <vector>

namespace Nyx {

class EngineContext;

class ProjectManager final {
public:
  ProjectManager() = default;

  void init(EngineContext &engine, std::string absEditorCfgPath);
  void shutdown();

  // Load editor config (recents, etc.)
  void loadEditorConfig();
  void saveEditorConfig();

  // Open existing project from absolute .nyxproj path
  bool openProject(const std::string &absNyxprojPath);
  bool openProjectFile(const std::string &nyxprojAbs) {
    return openProject(nyxprojAbs);
  }

  // Create a new project at absNyxprojPath (overwrites), with default content
  // folders.
  bool createProject(const std::string &absNyxprojPath,
                     const std::string &projectName,
                     bool createDefaultFolders = true);
  bool createProjectAt(const std::string &projectRootAbs,
                       const std::string &name);

  bool hasProject() const { return m_proj.hasProject(); }
  const NyxProjectRuntime &runtime() const { return m_proj; }
  NyxProjectRuntime &runtime() { return m_proj; }
  const std::string &projectRootAbs() const { return m_proj.rootAbs(); }
  std::string contentAbs() const { return m_proj.makeAbsolute("Content"); }
  std::string contentRel() const { return "Content"; }
  const std::vector<std::string> &recent() const { return m_userCfg.recent.items; }
  void addRecent(const std::string &absNyxproj);

  EditorUserConfig &userCfg() { return m_userCfg; }
  const EditorUserConfig &userCfg() const { return m_userCfg; }

  const std::string &editorCfgPathAbs() const { return m_editorCfgAbs; }

private:
  EngineContext *m_engine = nullptr;
  std::string m_editorCfgAbs;

  NyxProjectRuntime m_proj;
  EditorUserConfig m_userCfg;

  bool makeDefaultFolders(const std::string &projectDirAbs,
                          const std::string &assetRootRel);
};

} // namespace Nyx
