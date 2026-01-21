#include "CubeGenerator.h"
#include "glm/fwd.hpp"
#include "npgms/MeshCPU.h"

namespace Nyx {

// 24 verts (4 per face) so normals are per-face, correct for lighting
MeshCPU makeCubePN(const CubeDesc &d) {
  const float h = d.halfExtent;

  MeshCPU m{};
  m.vertices.reserve(24);
  m.indices.reserve(36);

  auto pushFace = [&](glm::vec3 n, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                      glm::vec3 d0) {
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({a, n});
    m.vertices.push_back({b, n});
    m.vertices.push_back({c, n});
    m.vertices.push_back({d0, n});

    m.indices.push_back(base + 0);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 0);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 3);
  };

  pushFace({1, 0, 0}, {h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h});
  pushFace({-1, 0, 0}, {-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h});
  pushFace({0, 1, 0}, {-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h});
  pushFace({0, -1, 0}, {-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h});
  pushFace({0, 0, 1}, {h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h});
  pushFace({0, 0, -1}, {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h});

  return m;
}

} // namespace Nyx
