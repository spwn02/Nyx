#pragma once

#include <cstdint>

namespace Nyx {

// Small stable handle (index+generation)
struct RGHandle {
  uint32_t idx = 0;
  uint32_t gen = 0;
};

inline bool operator==(RGHandle a, RGHandle b) {
  return a.idx == b.idx && a.gen == b.gen;
}
inline bool operator!=(RGHandle a, RGHandle b) { return !(a == b); }

static constexpr RGHandle InvalidRG{0u, 0u};

using RGTexHandle = RGHandle;
using RGBufHandle = RGHandle;

} // namespace Nyx
