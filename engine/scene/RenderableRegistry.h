#pragma once

#include "Renderable.h"
#include "scene/EntityID.h"
#include <vector>

namespace Nyx {

class RenderableRegistry {
public:
  Renderable &create(EntityID id);
  void clear();

  const std::vector<Renderable> &all() const { return m_renderables; }

private:
  std::vector<Renderable> m_renderables;
};

} // namespace Nyx
