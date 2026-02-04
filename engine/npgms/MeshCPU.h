#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Nyx {

// Tangent is vec4: xyz = tangent, w = handedness sign (bitangnt = sign *
// cross(N,T))
struct VertexPNut {
  glm::vec3 pos;
  glm::vec3 nrm;
  glm::vec2 uv;
  glm::vec4 tan; // (0,0,0,0) means "no tangent"
};

struct MeshCPU {
  std::vector<VertexPNut> vertices;
  std::vector<uint32_t> indices;
};

inline bool hasTangents(const MeshCPU &m) {
  for (const auto &v : m.vertices) {
    if (glm::length(glm::vec3(v.tan)) > 0.0001f)
      return true;
  }
  return false;
}

} // namespace Nyx
