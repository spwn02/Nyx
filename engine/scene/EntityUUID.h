#pragma once
#include <cstdint>
#include <cstddef>

namespace Nyx {

// Stable identifier for serialization. 64-bit is enough for editor scenes.
struct EntityUUID final {
  uint64_t value = 0;

  explicit operator bool() const { return value != 0; }

  bool operator==(const EntityUUID &o) const { return value == o.value; }
  bool operator!=(const EntityUUID &o) const { return value != o.value; }
  bool operator<(const EntityUUID &o) const { return value < o.value; }
};

struct EntityUUIDHash final {
  size_t operator()(const EntityUUID &id) const noexcept {
    uint64_t x = id.value;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
  }
};

// Simple generator (xorshift64*) with a seed you can set.
class EntityUUIDGen final {
public:
  EntityUUIDGen() = default;
  explicit EntityUUIDGen(uint64_t seed) { setSeed(seed); }

  void setSeed(uint64_t seed);
  uint64_t seed() const { return m_state; }

  EntityUUID next();

private:
  uint64_t m_state = 0xC0FFEE1234ULL; // default non-zero
};

} // namespace Nyx
