#pragma once

#include <cstdint>

namespace Nyx {

struct MaterialHandle {
  uint32_t slot = 0;
  uint32_t gen = 0;

  friend inline bool operator==(const MaterialHandle &a,
                                const MaterialHandle &b) {
    return a.slot == b.slot && a.gen == b.gen;
  }

  friend inline bool operator!=(const MaterialHandle &a,
                                const MaterialHandle &b) {
    return !(a == b);
  }
};

static constexpr MaterialHandle InvalidMaterial{0u, 0u};

} // namespace Nyx
