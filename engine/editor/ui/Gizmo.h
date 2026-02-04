#pragma once

#include "Selection.h"
#include "scene/World.h"
#include <glm/glm.hpp>

namespace Nyx {

class Gizmo final {
public:
  void drawAndInteract(World &world, Selection &sel, const glm::mat4 &view,
                       const glm::mat4 &proj, const glm::vec2 &viewportMin,
                       const glm::vec2 &viewportMax);

private:
  bool m_dragging = false;
  int m_axis = -1; // 0=X 1=Y 2=Z
  glm::vec3 m_dragStartWorld{0};
  glm::vec3 m_startTranslation{0};
};

} // namespace Nyx
