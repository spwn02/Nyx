#pragma once

#include "FilterRegistry.h"
#include <cstdint>
#include <vector>

namespace Nyx {

// Chain-only graph (linear). Nodes can be enabled/disabled.
// Bloom stays separate (not represented here).
class FilterGraph final {
public:
  void clear() { m_nodes.clear(); }

  uint32_t size() const { return m_nodes.size(); }
  bool empty() const { return m_nodes.empty(); }

  const std::vector<FilterNode> &nodes() const { return m_nodes; }
  std::vector<FilterNode> &nodes() { return m_nodes; }

  void addNode(FilterNode n) { m_nodes.push_back(std::move(n)); }
  void insertNode(uint32_t idx, FilterNode n) {
    if (idx >= m_nodes.size()) {
      m_nodes.push_back(std::move(n));
      return;
    }
    m_nodes.insert(m_nodes.begin() + idx, std::move(n));
  }
  void removeNode(uint32_t idx) {
    if (idx >= m_nodes.size())
      return;
    m_nodes.erase(m_nodes.begin() + idx);
  }
  void moveNode(uint32_t from, uint32_t to) {
    if (from >= m_nodes.size() || to >= m_nodes.size() || from == to)
      return;
    auto node = std::move(m_nodes[from]);
    m_nodes.erase(m_nodes.begin() + from);
    m_nodes.insert(m_nodes.begin() + to, std::move(node));
  }

private:
  std::vector<FilterNode> m_nodes;
};

} // namespace Nyx
