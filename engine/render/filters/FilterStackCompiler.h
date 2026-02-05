#pragma once

#include "FilterStackGPU.h"
#include "post/FilterGraph.h"
#include "post/FilterRegistry.h"
#include <cstdint>
#include <vector>

namespace Nyx {

struct CompiledFilterStack final {
  // raw bytes for SSBO upload
  std::vector<uint8_t> bytes;
  uint32_t nodeCount = 0;
};

// Compiles FilterGraph -> GPU buffer blob.
// Validates types/param counts; clamps/zeros as needed.
class FilterStackCompiler final {
public:
  explicit FilterStackCompiler(const FilterRegistry &reg) : m_reg(reg) {}

  CompiledFilterStack compile(const FilterGraph &g) const;

  // For change detection.
  static uint64_t hashBytes(const std::vector<uint8_t> &b);

private:
  const FilterRegistry &m_reg;
};

} // namespace Nyx
