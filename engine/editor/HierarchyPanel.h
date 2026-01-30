#pragma once

#include "IconAtlas.h"
#include "Selection.h"
#include "scene/EntityID.h"
#include "scene/World.h"
#include <vector>

namespace Nyx {

class HierarchyPanel {
public:
  void setWorld(World *world);
  void onWorldEvent(World &world, const WorldEvent &e);
  void draw(World &world, Selection &sel);

private:
  void drawEntityNode(World &world, EntityID e, Selection &sel);
  void rebuildRoots(World &world);
  void addRoot(EntityID e);
  void removeRoot(EntityID e);

private:
  std::vector<EntityID> m_roots;
  std::vector<EntityID> m_visibleOrder;
  IconAtlas m_iconAtlas{};
  bool m_iconInit = false;
  bool m_iconReady = false;
};

} // namespace Nyx
