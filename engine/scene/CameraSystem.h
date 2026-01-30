#pragma once

#include "World.h"
#include <glm/glm.hpp>

namespace Nyx {

class CameraSystem final {
public:
  // Update matrices for ALL cameras (or just active camera if you prefer later)
  void update(World &world, uint32_t viewportW, uint32_t viewportH);

  // Convenience: ensure active camera matrices are up to date and return VP
  glm::mat4 activeViewProj(World &world, uint32_t viewportW,
                           uint32_t viewportH);
};

} // namespace Nyx
