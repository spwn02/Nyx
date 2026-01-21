#pragma once

#include "Selection.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <vector>

namespace Nyx {

class HierarchyPanel {
public:
  void draw(World &world, Selection &sel);

private:
  void drawEntityNode(World &world, EntityID e, Selection &sel);

private:
  std::vector<EntityID> m_visibleOrder;
};

} // namespace Nyx
