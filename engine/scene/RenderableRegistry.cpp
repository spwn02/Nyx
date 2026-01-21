#include "RenderableRegistry.h"
#include "scene/EntityID.h"
#include "scene/Renderable.h"

namespace Nyx {

Renderable &RenderableRegistry::create(EntityID id) {
  Renderable r{};
  r.entity = id;
  m_renderables.push_back(r);
  return m_renderables.back();
}

void RenderableRegistry::clear() { m_renderables.clear(); }

} // namespace Nyx
