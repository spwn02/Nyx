#pragma once

#include "MeshCPU.h"
#include <glm/glm.hpp>
#include <vector>

namespace Nyx {

// MikkTSpace is later. This is a simple per-triangle tangent accumulation.
// Requirements: positions + normals + UVs + indexed triangles.
inline void generateTangents(MeshCPU &m) {
  const size_t vcount = m.vertices.size();
  if (vcount == 0 || m.indices.size() < 3)
    return;

  std::vector<glm::vec3> tanSum(vcount, glm::vec3(0.0f));
  std::vector<glm::vec3> bitSum(vcount, glm::vec3(0.0f));

  for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
    const uint32_t i0 = m.indices[i + 0];
    const uint32_t i1 = m.indices[i + 1];
    const uint32_t i2 = m.indices[i + 2];
    if (i0 >= vcount || i1 >= vcount || i2 >= vcount)
      continue;

    const auto &v0 = m.vertices[i0];
    const auto &v1 = m.vertices[i1];
    const auto &v2 = m.vertices[i2];

    const glm::vec3 p0 = v0.pos;
    const glm::vec3 p1 = v1.pos;
    const glm::vec3 p2 = v2.pos;

    const glm::vec2 w0 = v0.uv;
    const glm::vec2 w1 = v1.uv;
    const glm::vec2 w2 = v2.uv;

    const glm::vec3 e1 = p1 - p0;
    const glm::vec3 e2 = p2 - p0;
    const glm::vec2 d1 = w1 - w0;
    const glm::vec2 d2 = w2 - w0;

    const float det = d1.x * d2.y - d2.x * d1.y;
    if (glm::abs(det) < 1e-8f)
      continue;
    const float invDet = 1.0f / det;

    glm::vec3 T = (e1 * d2.y - e2 * d1.y) * invDet;
    glm::vec3 B = (e2 * d1.x - e1 * d2.x) * invDet;

    tanSum[i0] += T;
    tanSum[i1] += T;
    tanSum[i2] += T;

    bitSum[i0] += B;
    bitSum[i1] += B;
    bitSum[i2] += B;
  }

  for (size_t i = 0; i < vcount; ++i) {
    glm::vec3 N = glm::normalize(m.vertices[i].nrm);
    glm::vec3 T = tanSum[i];

    if (glm::length(T) < 1e-6f) {
      m.vertices[i].tan = glm::vec4(0, 0, 0, 0);
      continue;
    }

    // Orthonormalize T vs N
    T = glm::normalize(T - N * glm::dot(N, T));

    // Handedness
    glm::vec3 B = bitSum[i];
    float sign = glm::dot(glm::cross(N, T), B) < 0.0f ? -1.0f : 1.0f;

    m.vertices[i].tan = glm::vec4(T, sign);
  }
}

} // namespace Nyx
