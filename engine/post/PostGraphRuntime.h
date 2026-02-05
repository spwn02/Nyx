#pragma once

#include "PostGraph.h"

namespace Nyx {

// Owns CPU compiled stack + dirty tracking.
// GPU upload lives in post/tonemap system.
class PostGraphRuntime final {
public:
  void setGraph(PostGraph *g) { m_graph = g; }

  // Recompile if changed, returns true if outputs changes (upload needed).
  bool recompile(const class FilterRegistry &reg);

  const FilterStackCPU &stack() const { return m_stack; }

  void markDirty() { m_dirty = true; }

private:
  PostGraph *m_graph = nullptr;
  bool m_dirty = true;

  FilterStackCPU m_stack{};
};

} // namespace Nyx
