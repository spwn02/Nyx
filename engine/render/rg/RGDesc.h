#pragma once

#include "RGFormat.h"
#include <cstdint>

namespace Nyx {

enum class RGTexUsage : uint32_t {
  None = 0,
  ColorAttach = 1u << 0,
  DepthAttach = 1u << 1,
  Sampled = 1u << 2,
  Image = 1u << 3,
  Storage = 1u << 4,
};

inline RGTexUsage operator|(RGTexUsage a, RGTexUsage b) {
  return (RGTexUsage)((uint32_t)a | (uint32_t)b);
}
inline RGTexUsage operator&(RGTexUsage a, RGTexUsage b) {
  return (RGTexUsage)((uint32_t)a & (uint32_t)b);
}
inline RGTexUsage &operator|=(RGTexUsage &a, RGTexUsage b) {
  a = a | b;
  return a;
}
inline bool hasUsage(RGTexUsage v, RGTexUsage mask) {
  return (((uint32_t)v) & (uint32_t)mask) != 0u;
}

struct RGTexDesc {
  uint32_t w = 1;
  uint32_t h = 1;
  RGFormat fmt = RGFormat::RGBA8;
  RGTexUsage usage = RGTexUsage::None;

  bool operator==(const RGTexDesc &other) const {
    return w == other.w && h == other.h && fmt == other.fmt &&
           usage == other.usage;
  }
};

} // namespace Nyx
