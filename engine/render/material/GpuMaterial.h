#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

// Keep std430 friendly alignment (vec4 aligned)
struct GpuMaterialPacked final {
  glm::vec4 baseColorFactor; // rgba
  glm::vec4 emissiveFactor;  // rgb + pad

  glm::vec4 mrAoFlags; // metallic, roughness, ao, flags

  // texture indices into texture table (or 0xFFFFFFFF for none)
  // baseColor, emissive, normal, metalRough, ao
  glm::uvec4 tex0123;  // base/emis/norm/mr
  glm::uvec4 tex4_pad; // occ, ext, pad, pad

  glm::vec4 uvScaleOffset; // xy=scale, zw=offset
};

static constexpr uint32_t kInvalidTexIndex = 0xFFFFFFFF;

} // namespace Nyx
