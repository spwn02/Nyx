#pragma once

#include <cstdint>
#include <string>

namespace Nyx {

enum class MaterialTexSlot : uint8_t {
  BaseColor = 0,
  Emissive = 1,
  Normal = 2,
  Metallic = 3,
  Roughness = 4,
  AO = 5,
  Count
};

enum class MatAlphaMode : uint32_t { Opaque = 0, Mask = 1, Blend = 2 };

// Bitflags for GPU material features
enum MaterialFlags : uint32_t {
  Mat_None = 0,
  Mat_HasBaseColor = 1u << 0,
  Mat_HasEmissive = 1u << 1,
  Mat_HasNormal = 1u << 2,
  Mat_HasMetallic = 1u << 3,
  Mat_HasRoughness = 1u << 4,
  Mat_HasAO = 1u << 5,
  Mat_HasTangents = 1u << 6, // mesh provides tangents
  Mat_TangentSpaceNormal = 1u << 7, // material expects tangent-space normals
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

inline const char *materialSlotName(MaterialTexSlot s) {
  switch (s) {
  case MaterialTexSlot::BaseColor:
    return "Base Color";
  case MaterialTexSlot::Emissive:
    return "Emissive";
  case MaterialTexSlot::Normal:
    return "Normal";
  case MaterialTexSlot::Metallic:
    return "Metallic";
  case MaterialTexSlot::Roughness:
    return "Roughness";
  case MaterialTexSlot::AO:
    return "AO";
  default:
    return "Slot";
  }
}

inline bool materialSlotWantsSRGB(MaterialTexSlot s) {
  return (s == MaterialTexSlot::BaseColor || s == MaterialTexSlot::Emissive);
}

struct MaterialSlotRef final {
  std::string path;               // absolute or project-relative
  uint32_t texIndex = 0xFFFFFFFF; // TextureTable index
  bool srgb = false;              // must match materialSlotWantsSRGB(slot)
};

} // namespace Nyx
