#pragma once

#include <cstdint>

namespace Nyx {

// VM limits (tweak later)
static constexpr uint32_t kMatVM_MaxRegs = 128;
static constexpr uint32_t kMatVM_MaxNodes = 512;

// Packed ops (GPU)
enum class MatOp : uint32_t {
  // constants / wiring
  Const4 = 0,
  Swizzle,
  Append,

  // math
  Add,
  Sub,
  Mul,
  Div,
  Min,
  Max,
  Clamp01,
  OneMinus,
  Lerp,
  Pow,
  Dot3,
  Normalize3,

  // textures
  Tex2D,       // generic
  Tex2D_SRGB,  // auto srgb->linear
  Tex2D_MRA,   // packed: R=metallic, G=roughness, B=ao (convention)
  NormalMapTS, // tangent-space normal map decode -> world normal

  // output (writes final Surface slots from regs)
  OutputSurface,
};

// Node encoding (std430 friendly)
struct GpuMatNode final {
  uint32_t op;    // MatOp
  uint32_t dst;   // reg index
  uint32_t a;     // reg or param
  uint32_t b;     // reg or param
  uint32_t c;     // reg or param
  uint32_t extra; // packed params / flags / texture indices
};

// Per-material header
enum class AlphaMode : uint32_t { Opaque = 0, Mask = 1, Blend = 2 };

struct GpuMatGraphHeader final {
  uint32_t nodeOffset;
  uint32_t nodeCount;

  // output regs
  uint32_t outBaseColor; // vec3 in reg.xyz
  uint32_t outMR;        // x=metallic, y=roughness, z=ao
  uint32_t outNormalWS;    // vec3 in reg.xyz
  uint32_t outEmissive;  // vec3 in reg.xyz
  uint32_t outAlpha;     // float in reg.x

  uint32_t alphaMode; // AlphaMode
  float alphaCutoff;  // for Mask
  uint32_t _pad0;
  uint32_t _pad1;
};

// SSBO layout: headers + nodes
struct GpuMatGraphTables final {
  // not used directly (for clarity)
};

} // namespace Nyx
