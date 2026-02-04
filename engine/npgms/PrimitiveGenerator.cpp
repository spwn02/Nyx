#include "PrimitiveGenerator.h"
#include "glm/gtc/constants.hpp"

#include <cmath>
#include <glm/glm.hpp>

#include "MeshTangentGen.h"

namespace Nyx {

static MeshCPU makePlanePNUt(float halfExtent) {
  MeshCPU m{};
  // UVs 0..1
  m.vertices = {
      {{-halfExtent, 0.0f, -halfExtent}, {0, 1, 0}, {0, 0}, {0, 0, 0, 0}},
      {{halfExtent, 0.0f, -halfExtent}, {0, 1, 0}, {1, 0}, {0, 0, 0, 0}},
      {{halfExtent, 0.0f, halfExtent}, {0, 1, 0}, {1, 1}, {0, 0, 0, 0}},
      {{-halfExtent, 0.0f, halfExtent}, {0, 1, 0}, {0, 1}, {0, 0, 0, 0}},
  };
  m.indices = {0, 1, 2, 0, 2, 3};

  generateTangents(m);
  return m;
}

// Cube with per-face UVs (no shared verts) so tangents are correct per face.
static MeshCPU makeCubePNUt(float halfExtent) {
  MeshCPU m{};
  m.vertices.reserve(24);
  m.indices.reserve(36);

  auto pushFace = [&](glm::vec3 n, glm::vec3 a, glm::vec3 b, glm::vec3 c,
                      glm::vec3 d) {
    // UVs: a(0,0) b(1,0) c(1,1) d(0,1)
    const uint32_t base = (uint32_t)m.vertices.size();
    m.vertices.push_back({a, n, {0, 0}, {0, 0, 0, 0}});
    m.vertices.push_back({b, n, {1, 0}, {0, 0, 0, 0}});
    m.vertices.push_back({c, n, {1, 1}, {0, 0, 0, 0}});
    m.vertices.push_back({d, n, {0, 1}, {0, 0, 0, 0}});
    m.indices.insert(m.indices.end(), {base + 0, base + 1, base + 2, base + 0,
                                       base + 2, base + 3});
  };

  const float h = halfExtent;

  // +Z
  pushFace({0, 0, 1}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h});
  // -Z
  pushFace({0, 0, -1}, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h});
  // +X
  pushFace({1, 0, 0}, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h});
  // -X
  pushFace({-1, 0, 0}, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h});
  // +Y
  pushFace({0, 1, 0}, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h});
  // -Y
  pushFace({0, -1, 0}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h});

  generateTangents(m);
  return m;
}

static MeshCPU makeCirclePNUt(uint32_t seg, float radius = 0.5f) {
  if (seg < 3)
    seg = 3;

  MeshCPU m{};
  m.vertices.reserve(seg + 1);
  m.indices.reserve(seg * 3);

  // center
  m.vertices.push_back({{0, 0, 0}, {0, 1, 0}, {0.5f, 0.5f}, {0, 0, 0, 0}});

  const float step = glm::two_pi<float>() / float(seg);
  for (uint32_t i = 0; i < seg; ++i) {
    float a = step * float(i);
    float x = std::cos(a) * radius;
    float z = std::sin(a) * radius;
    float u = (x / radius) * 0.5f + 0.5f;
    float v = (z / radius) * 0.5f + 0.5f;
    m.vertices.push_back({{x, 0, z}, {0, 1, 0}, {u, v}, {0, 0, 0, 0}});
  }

  for (uint32_t i = 0; i < seg; ++i) {
    uint32_t a = 0;
    uint32_t b = 1 + i;
    uint32_t c = 1 + ((i + 1) % seg);
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
  }

  generateTangents(m);
  return m;
}

static MeshCPU makeSpherePNUt(uint32_t segU, uint32_t segV,
                              float radius = 0.5f) {
  segU = (segU < 8) ? 8 : segU;
  segV = (segV < 6) ? 6 : segV;

  MeshCPU m{};
  m.vertices.reserve((segU + 1) * (segV + 1));
  m.indices.reserve(segU * segV * 6);

  for (uint32_t y = 0; y <= segV; ++y) {
    float v = float(y) / float(segV);
    float phi = v * glm::pi<float>(); // 0..pi

    for (uint32_t x = 0; x <= segU; ++x) {
      float u = float(x) / float(segU);
      float theta = u * glm::two_pi<float>(); // 0..2pi

      glm::vec3 n{std::cos(theta) * std::sin(phi), std::cos(phi),
                  std::sin(theta) * std::sin(phi)};
      glm::vec3 p = n * radius;

      m.vertices.push_back({p, glm::normalize(n), {u, 1.0f - v},
                             {0, 0, 0, 0}});
    }
  }

  auto idx = [&](uint32_t x, uint32_t y) { return y * (segU + 1) + x; };

  for (uint32_t y = 0; y < segV; ++y) {
    for (uint32_t x = 0; x < segU; ++x) {
      uint32_t i0 = idx(x, y);
      uint32_t i1 = idx(x + 1, y);
      uint32_t i2 = idx(x + 1, y + 1);
      uint32_t i3 = idx(x, y + 1);

      m.indices.push_back(i0);
      m.indices.push_back(i1);
      m.indices.push_back(i2);
      m.indices.push_back(i0);
      m.indices.push_back(i2);
      m.indices.push_back(i3);
    }
  }

  generateTangents(m);
  return m;
}

MeshCPU makePrimitivePN(ProcMeshType type, uint32_t detail) {
  switch (type) {
  case ProcMeshType::Cube:
    return makeCubePNUt(0.5f);
  case ProcMeshType::Plane:
    return makePlanePNUt(0.5f);
  case ProcMeshType::Circle:
    return makeCirclePNUt(detail, 0.5f);
  case ProcMeshType::Sphere:
    return makeSpherePNUt(detail, detail / 2u, 0.5f);
  case ProcMeshType::Monkey:
    // Placeholder: keep pipeline working; real Suzanne comes soon.
    return makeCubePNUt(0.5f);
  default:
    return makeCubePNUt(0.5f);
  }
}

} // namespace Nyx
