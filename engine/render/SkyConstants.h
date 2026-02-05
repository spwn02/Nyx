#pragma once
#include <glm/glm.hpp>

namespace Nyx {

struct SkyConstants final {
  glm::mat4 invViewProj{1.0f};
  glm::vec4 camPos{0, 0, 0, 0};
  glm::vec4 skyParams{1, 0, 0, 1}; // intensity, exposureStops, yawRad, drawBackground
  glm::vec4 skyParams2{0.03f, 0, 0, 0}; // ambient, reserved
};

} // namespace Nyx
