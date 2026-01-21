#include "GLMesh.h"
#include "../../core/Assert.h"
#include <glad/glad.h>

namespace Nyx {

GLMesh::~GLMesh() {
  if (m_ebo)
    glDeleteBuffers(1, &m_ebo);
  if (m_vbo)
    glDeleteBuffers(1, &m_vbo);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
}

void GLMesh::upload(const MeshCPU &cpu) {
  NYX_ASSERT(!cpu.vertices.empty(), "GLMesh upload: no vertices");
  NYX_ASSERT(!cpu.indices.empty(), "GLMesh upload: no indices");

  m_indexCount = static_cast<uint32_t>(cpu.indices.size());

  if (!m_vao)
    glCreateVertexArrays(1, &m_vao);
  if (!m_vbo)
    glCreateBuffers(1, &m_vbo);
  if (!m_ebo)
    glCreateBuffers(1, &m_ebo);

  glNamedBufferData(
      m_vbo, static_cast<GLsizeiptr>(cpu.vertices.size() * sizeof(VertexPN)),
      cpu.vertices.data(), GL_STATIC_DRAW);

  glNamedBufferData(
      m_ebo, static_cast<GLsizeiptr>(cpu.indices.size() * sizeof(uint32_t)),
      cpu.indices.data(), GL_STATIC_DRAW);

  glVertexArrayVertexBuffer(m_vao, 0, m_vbo, 0, sizeof(VertexPN));
  glVertexArrayElementBuffer(m_vao, m_ebo);

  // layout(location=0) vec3 aPos
  glEnableVertexArrayAttrib(m_vao, 0);
  glVertexArrayAttribFormat(m_vao, 0, 3, GL_FLOAT, GL_FALSE,
                            offsetof(VertexPN, position));
  glVertexArrayAttribBinding(m_vao, 0, 0);

  // layout(location=1) vec3 aNrm
  glEnableVertexArrayAttrib(m_vao, 1);
  glVertexArrayAttribFormat(m_vao, 1, 3, GL_FLOAT, GL_FALSE,
                            offsetof(VertexPN, normal));
  glVertexArrayAttribBinding(m_vao, 1, 0);
}

void GLMesh::draw() const {
  if (!m_vao || m_indexCount == 0)
    return;
  glBindVertexArray(m_vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount),
                 GL_UNSIGNED_INT, nullptr);
}

} // namespace Nyx
