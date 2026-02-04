#pragma once

#include "scene/material/MaterialData.h"
#include <glm/glm.hpp>
#include <string>

namespace Nyx {

struct Vertex {
  glm::vec3 pos{0.0f};
  glm::vec3 nrm{0.0f, 1.0f, 0.0f};
  glm::vec4 tan{1.0f, 0.0f, 0.0f, 1.0f}; // xyz tangent, w = handedness
  glm::vec2 uv0{0.0f};
  glm::vec4 color0{1.0f};  // optional
  
  glm::uvec4 joints{0u}; // optional
  glm::vec4 weights{0.0f}; // optional
};

struct SubmeshCPU {
  std::string name{"Submesh"};
  uint32_t firstIndex = 0;
  uint32_t indexCount = 0;
  // uint32_t firstVertex = 0;
  uint32_t materialSlot = 0; // points into materials[0] for this asset

  glm::vec3 aabbMin{0.0f}, aabbMax{0.0f};
};

struct MeshAssetCPU {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<SubmeshCPU> submeshes;

  std::vector<MaterialData> materials;
};

} // namespace Nyx
