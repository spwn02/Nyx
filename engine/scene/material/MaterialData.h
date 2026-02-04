#pragma once

#include "MaterialTypes.h"
#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <string>

namespace Nyx {

struct MaterialData {
  glm::vec4 baseColorFactor{1, 1, 1, 1};
  glm::vec3 emissiveFactor{0, 0, 0};

  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;

  // Texture asset paths. Empty => unbound.
  std::array<std::string, static_cast<size_t>(MaterialTexSlot::Count)>
      texPath{};

  // UV scale/offset if you want (kept minimal)
  glm::vec2 uvScale{1, 1};
  glm::vec2 uvOffset{0, 0};
};

} // namespace Nyx
