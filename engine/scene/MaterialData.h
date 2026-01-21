#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

struct MaterialData {
  glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;

  bool alphaMasked = false;
  float alphaCutoff = 0.5f;
};

} // namespace Nyx
