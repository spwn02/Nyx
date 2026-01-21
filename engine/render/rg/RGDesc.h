#pragma once

#include "RGFormat.h"
#include <cstdint>

namespace Nyx {

struct RGTexDesc {
  uint32_t w = 1;
  uint32_t h = 1;
  RGFormat fmt = RGFormat::RGBA8;

  bool operator==(const RGTexDesc &other) const {
    return w == other.w && h == other.h && fmt == other.fmt;
  }
};

} // namespace Nyx
