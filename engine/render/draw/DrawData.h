#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

// std430 alignment friendly (mat4 = 64 bytes; keep uvec4/vec4 aligned).
struct DrawData final {
  glm::mat4 model{1.0f};
  uint32_t materialIndex = 0;
  uint32_t pickID = 0;
  uint32_t meshHandle = 0; // mesh primitive handle/id
  uint32_t _pad0 = 0;
};

} // namespace Nyx
