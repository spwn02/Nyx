#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Nyx {

struct VertexPNut {
  glm::vec3 pos{0.0f};
  glm::vec3 nrm{0.0f, 1.0f, 0.0f};
  glm::vec2 uv{0.0f};
  glm::vec4 tan{1.0f, 0.0, 0.0f, 1.0f}; // xyz = tangent, w = handedness
};

struct MeshCPU {
  std::vector<VertexPNut> vertices;
  std::vector<uint32_t> indices;

  bool hasTangents = false;
};

} // namespace Nyx
