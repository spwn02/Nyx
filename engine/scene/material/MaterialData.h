#pragma once

#include "MaterialTypes.h"
#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <string>

namespace Nyx {

struct MaterialData {
  std::string name;
  glm::vec4 baseColorFactor{1, 1, 1, 1};
  glm::vec3 emissiveFactor{0, 0, 0};

  float metallic = 0.0f;
  float roughness = 0.5f;
  float ao = 1.0f;

  // Alpha
  MatAlphaMode alphaMode = MatAlphaMode::Opaque;
  float alphaCutoff = 0.5f; // only used for Mask

  // Texture asset paths. Empty => unbound.
  std::array<std::string, static_cast<size_t>(MaterialTexSlot::Count)>
      texPath{};

  // UV scale/offset if you want (kept minimal)
  glm::vec2 uvScale{1, 1};
  glm::vec2 uvOffset{0, 0};

  // Flags
  bool tangentSpaceNormal = true;
};

struct MaterialValidation final {
  bool ok = true;
  bool warn = false;
  std::string message;
};

MaterialValidation validateMaterial(const MaterialData &m);

} // namespace Nyx
