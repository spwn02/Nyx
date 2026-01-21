#pragma once

#include <cstdint>

namespace Nyx {

enum class RGFormat : uint8_t {
  RGBA16F,
  RGBA8,
  R32UI,
  Depth32F
};

} // namespace Nyx
