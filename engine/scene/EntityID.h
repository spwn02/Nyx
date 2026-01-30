#pragma once

#include <cstddef>
#include <cstdint>

namespace Nyx {

// EntityID = 32-bit (index) + 32-bit (generation)
struct EntityID final {
  uint32_t index = 0;
  uint32_t generation = 0;

  constexpr bool operator==(const EntityID &o) const {
    return index == o.index && generation == o.generation;
  }
  constexpr bool operator!=(const EntityID &o) const { return !(*this == o); }
  constexpr explicit operator bool() const { return generation != 0; }

  constexpr bool operator<(const EntityID &o) const {
    return index != o.index ? index < o.index : generation < o.generation;
  }
};

inline constexpr EntityID InvalidEntity{0u, 0u};

// Hash for unordered_map
struct EntityHash {
  size_t operator()(const EntityID &e) const noexcept {
    return (size_t(e.index) << 32) ^ size_t(e.generation);
  }
};

} // namespace Nyx
