#pragma once

#include "FilterStackCompiler.h"
#include "post/FilterGraph.h"
#include "post/FilterRegistry.h"
#include <cstdint>

namespace Nyx {

// Owns GPU SSBO for post-filter chain; uploads only when graph changes.
class FilterStackSSBO final {
public:
  void init(const FilterRegistry &registry);
  void shutdown();

  // Returns true if GPU buffer changed (uploaded).
  bool updateIfDirty(const FilterGraph &graph);

  uint32_t ssbo() const { return m_ssbo; }
  uint32_t nodeCount() const { return m_nodeCount; }

private:
  const FilterRegistry *m_registry = nullptr;
  uint32_t m_ssbo = 0;
  uint32_t m_nodeCount = 0;
  uint64_t m_lastHash = 0;

  FilterStackCompiler *m_compiler = nullptr; // owned
};

} // namespace Nyx
