#pragma once

#include "render/draw/DrawData.h"
#include <cstdint>
#include <vector>

namespace Nyx {

class PerDrawSSBO final {
public:
  void init();
  void shutdown();

  // Upload DrawData[] for this frame. Realloc if needed.
  void upload(const std::vector<DrawData> &draws);

  uint32_t ssbo() const { return m_ssbo; }
  uint32_t count() const { return m_count; }

private:
  uint32_t m_ssbo = 0;
  uint32_t m_count = 0;
  uint32_t m_capacity = 0; // in elements
};

} // namespace Nyx
