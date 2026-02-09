#pragma once

#include "project/ProjectManager.h"

namespace Nyx {

class ProjectPanel final {
public:
  void draw(ProjectManager &pm);

private:
  char m_newName[128] = "NyxProject";
  char m_newRoot[512] = "";
};

} // namespace Nyx
