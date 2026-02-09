#pragma once
#include "project/ProjectManager.h"
#include <string>

namespace Nyx {

class ProjectBrowserPanel final {
public:
  void draw(ProjectManager &pm);

  // Optional: show as modal on startup when no project loaded
  void openModal() { m_open = true; }

private:
  bool m_open = false;
  bool m_closeBrowserNextFrame = false;
  std::string m_createError;
};

} // namespace Nyx
