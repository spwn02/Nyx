#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Nyx {

struct VertexPN {
  glm::vec3 position;
  glm::vec3 normal;
};

struct MeshCPU {
  std::vector<VertexPN> vertices;
  std::vector<uint32_t> indices;
};

} // namespace Nyx
