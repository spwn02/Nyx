#pragma once

#include <cstdint>

namespace Nyx {

enum class MaterialTexSlot : uint8_t {
  BaseColor = 0,
  Emissive = 1,
  Normal = 2,
  MetalRough = 3,
  AO = 4,
  Count
};

// Bitflags for GPU material features
enum MaterialFlags : uint32_t {
  Mat_None = 0,
  Mat_HasBaseColor = 1u << 0,
  Mat_HasEmissive = 1u << 1,
  Mat_HasNormal = 1u << 2,
  Mat_HasMetalRough = 1u << 3,
  Mat_HasAO = 1u << 4,
  Mat_HasTangents = 1u << 5, // mesh provides tangents
};

inline MaterialFlags operator|(MaterialFlags a, MaterialFlags b) {
  return static_cast<MaterialFlags>(static_cast<uint32_t>(a) |
                                    static_cast<uint32_t>(b));
}

inline MaterialFlags &operator|=(MaterialFlags &a, MaterialFlags b) {
  a = a | b;
  return a;
}

inline bool hasFlag(uint32_t flags, MaterialFlags f) {
  return (flags & static_cast<uint32_t>(f)) != 0;
}

} // namespace Nyx
