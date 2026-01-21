#pragma once

#include <glm/glm.hpp>

namespace Nyx {

struct DemoCube {
  glm::vec3 position{0.0f};
  glm::vec3 scale{1.0};
  glm::vec4 color{0.2f, 0.8, 0.95f, 1.0f};
};

} // namespace Nyx
