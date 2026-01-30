#pragma once

#include "glm/glm.hpp"
#include <cstdint>

namespace Nyx {

struct RenderPassContext {
  uint32_t windowWidth = 0;
  uint32_t windowHeight = 0;
  uint32_t viewportWidth = 0;
  uint32_t viewportHeight = 0;
  uint32_t fbWidth = 0;
  uint32_t fbHeight = 0;
  uint32_t frameIndex = 0;

  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  glm::mat4 viewProj{1.0f};

  glm::vec3 cameraPos{0.0f};
  glm::vec3 cameraDir{0.0f, 0.0f, -1.0f};
};

} // namespace Nyx
