#include "HierarchyPanel.h"

#include <algorithm>

namespace Nyx {

void HierarchyPanel::setWorld(World *world) {
  m_roots.clear();
  m_visibleOrder.clear();
  if (world)
    rebuildRoots(*world);
}

void HierarchyPanel::rebuildRoots(World &world) { m_roots = world.roots(); }

void HierarchyPanel::addRoot(EntityID e) {
  if (e == InvalidEntity)
    return;
  if (std::find(m_roots.begin(), m_roots.end(), e) != m_roots.end())
    return;

  auto it = std::lower_bound(m_roots.begin(), m_roots.end(), e,
                             [](EntityID a, EntityID b) {
                               if (a.index != b.index)
                                 return a.index < b.index;
                               return a.generation < b.generation;
                             });
  m_roots.insert(it, e);
}

void HierarchyPanel::removeRoot(EntityID e) {
  m_roots.erase(std::remove(m_roots.begin(), m_roots.end(), e), m_roots.end());
}

void HierarchyPanel::onWorldEvent(World &world, const WorldEvent &e) {
  switch (e.type) {
  case WorldEventType::EntityCreated:
    if (world.isAlive(e.a) && world.parentOf(e.a) == InvalidEntity)
      addRoot(e.a);
    break;
  case WorldEventType::EntityDestroyed:
    removeRoot(e.a);
    break;
  case WorldEventType::ParentChanged:
    if (e.b == InvalidEntity)
      addRoot(e.a);
    else
      removeRoot(e.a);
    break;
  default:
    break;
  }
}

} // namespace Nyx
