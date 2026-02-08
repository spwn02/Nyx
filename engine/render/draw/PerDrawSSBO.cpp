#include "render/draw/PerDrawSSBO.h"

#include <glad/glad.h>

namespace Nyx {

void PerDrawSSBO::init() {
  if (!m_ssbo)
    glCreateBuffers(1, &m_ssbo);
}

void PerDrawSSBO::shutdown() {
  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
  m_count = 0;
  m_capacity = 0;
}

void PerDrawSSBO::upload(const std::vector<DrawData> &draws) {
  if (!m_ssbo)
    init();

  m_count = static_cast<uint32_t>(draws.size());
  const uint32_t need = m_count;

  if (need == 0) {
    // keep buffer allocated; just set size = 0
    return;
  }

  if (need > m_capacity) {
    // grow with slack to avoid realloc each frame
    m_capacity = need + (need / 2u) + 64u;
    const GLsizeiptr bytes =
        static_cast<GLsizeiptr>(m_capacity * sizeof(DrawData));

    glNamedBufferData(m_ssbo, bytes, nullptr, GL_DYNAMIC_DRAW);
  }

  const GLsizeiptr uploadBytes =
      static_cast<GLsizeiptr>(need * sizeof(DrawData));
  glNamedBufferSubData(m_ssbo, 0, uploadBytes, draws.data());
}

} // namespace Nyx
