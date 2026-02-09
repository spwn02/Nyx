#include "HierarchyPanel.h"

namespace Nyx {

void HierarchyPanel::copyTransform(World &world, EntityID e) {
  if (!world.isAlive(e))
    return;

  const auto &t = world.transform(e);
  m_copyTranslation = t.translation;
  m_copyRotation = t.rotation;
  m_copyScale = t.scale;
  m_hasCopiedTransform = true;
}

void HierarchyPanel::pasteTransform(World &world, EntityID e) {
  if (!world.isAlive(e) || !m_hasCopiedTransform)
    return;

  auto &t = world.transform(e);
  t.translation = m_copyTranslation;
  t.rotation = m_copyRotation;
  t.scale = m_copyScale;
  t.dirty = true;
  world.worldTransform(e).dirty = true;
}

} // namespace Nyx
