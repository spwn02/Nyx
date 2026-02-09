#pragma once
#include "project/ProjectManager.h"
#include "scene/SceneManager.h"

namespace Nyx {

class SceneBrowserPanel final {
public:
  void draw(SceneManager &sm, ProjectManager &pm);

private:
  enum class PendingAction {
    None,
    OpenScene,
    NewScene,
  };

  PendingAction m_pendingAction = PendingAction::None;
  std::string m_pendingPathAbs;
  std::string m_lastError;

  void queueOpen(const std::string &absPath);
  void queueCreate(const std::string &absPath);
  bool executePending(SceneManager &sm);
};

} // namespace Nyx
