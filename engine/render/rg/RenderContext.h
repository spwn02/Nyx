#pragma once

#include "glm/glm.hpp"
#include <cstdint>

namespace Nyx {

struct RenderContext {
  uint32_t fbWidth = 0;
  uint32_t fbHeight = 0;
  uint32_t frameIndex = 0;
  glm::mat4 viewProj{1.0f};
};

} // namespace Nyx
