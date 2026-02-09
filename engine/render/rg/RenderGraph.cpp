#include "RenderGraph.h"

#include <utility>

namespace Nyx {

void RenderGraph::reset() {
  m_blackboard.reset();
  m_passes.clear();
  m_legacy.clear();
  m_lastOrder.clear();
  m_lastEdges.clear();
  m_lastLifetimes.clear();
  m_lastResolved.clear();
}

void RenderGraph::addPass(std::string name, SetupFn setup, ExecuteFn exec) {
  PassNode node{};
  node.name = std::move(name);
  node.setup = std::move(setup);
  node.exec = std::move(exec);
  node.order = (uint32_t)m_passes.size();

  RenderPassBuilder builder(m_blackboard, node.texUses, node.bufUses);
  if (node.setup)
    node.setup(builder);

  m_passes.push_back(std::move(node));
}

void RenderGraph::addPass(std::string name,
                          std::function<void(RGResources &)> fn) {
  m_legacy.push_back(LegacyPass{std::move(name), std::move(fn)});
}

void RenderGraph::execute(RGResources &r) {
  for (auto &p : m_legacy)
    p.exec(r);
}

} // namespace Nyx
