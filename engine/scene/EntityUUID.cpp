#include "EntityUUID.h"

namespace Nyx {

void EntityUUIDGen::setSeed(uint64_t seed) {
  // Must be non-zero for xorshift
  m_state = (seed == 0) ? 0x9E3779B97F4A7C15ULL : seed;
}

EntityUUID EntityUUIDGen::next() {
  // xorshift64*
  uint64_t x = m_state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  m_state = x;

  uint64_t v = x * 2685821657736338717ULL;

  // avoid 0 (reserved)
  if (v == 0)
    v = 1;

  return EntityUUID{v};
}

} // namespace Nyx
