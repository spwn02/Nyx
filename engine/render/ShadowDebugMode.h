#pragma once

#include <cstdint>

namespace Nyx {

enum class ShadowDebugMode : uint32_t {
  None = 0,
  CascadeIndex = 1,
  ShadowFactor = 2,
  ShadowMap0 = 3,
  ShadowMap1 = 4,
  ShadowMap2 = 5,
  ShadowMap3 = 6,
  Combined = 7,
};

} // namespace Nyx
