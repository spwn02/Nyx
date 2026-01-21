#pragma once

#include <cstdint>

namespace Nyx {

enum class ViewMode : uint8_t {
  Lit = 0,
  Albedo = 1,
  Normals = 2,
  Roughness = 3,
  Metallic = 4,
  AO = 5,
  Depth = 6,
  ID = 7
};

} // namespace Nyx
