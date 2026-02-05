#include "FilterStackSSBO.h"

#include <glad/glad.h>
#include <new>

namespace Nyx {

void FilterStackSSBO::init(const FilterRegistry &registry) {
  m_registry = &registry;
  if (!m_compiler)
    m_compiler = new (std::nothrow) FilterStackCompiler(registry);
  if (!m_ssbo)
    glCreateBuffers(1, &m_ssbo);

  m_nodeCount = 0;
  m_lastHash = 0;
}

void FilterStackSSBO::shutdown() {
  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
  delete m_compiler;
  m_compiler = nullptr;
  m_registry = nullptr;
  m_nodeCount = 0;
  m_lastHash = 0;
}

bool FilterStackSSBO::updateIfDirty(const FilterGraph &graph) {
  if (!m_compiler || !m_ssbo)
    return false;

  CompiledFilterStack cs = m_compiler->compile(graph);
  const uint64_t h = FilterStackCompiler::hashBytes(cs.bytes);

  if (h == m_lastHash && cs.nodeCount == m_nodeCount) {
    return false;
  }

  glNamedBufferData(m_ssbo, (GLsizeiptr)cs.bytes.size(), cs.bytes.data(),
                    GL_DYNAMIC_DRAW);

  m_lastHash = h;
  m_nodeCount = cs.nodeCount;
  return true;
}

} // namespace Nyx
