#pragma once

#include <cstdint>

namespace Nyx {

// GPU-side filter chain (SSBO).
static constexpr uint32_t kGpuFilterMaxParams = 16;

struct GpuFilterNode final {
  uint32_t type = 0;
  uint32_t enabled = 1;
  uint32_t paramCount = 0;
  uint32_t _pad = 0;
  float params[kGpuFilterMaxParams]{};
};

struct GpuFilterStackHeader final {
  uint32_t count = 0;
  uint32_t _pad0 = 0;
  uint32_t _pad1 = 0;
  uint32_t _pad2 = 0;
};

} // namespace Nyx
