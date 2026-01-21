#include "PrimitiveGenerator.h"
#include "CubeGenerator.h"
#include "glm/gtc/constants.hpp"

#include <cmath>
#include <glm/glm.hpp>

namespace Nyx {

static MeshCPU makePlanePN(float halfExtent = 0.5f) {
  MeshCPU m{};
  m.vertices = {
      {{-halfExtent, 0.0f, -halfExtent}, {0, 1, 0}},
      {{halfExtent, 0.0f, -halfExtent}, {0, 1, 0}},
      {{halfExtent, 0.0f, halfExtent}, {0, 1, 0}},
      {{-halfExtent, 0.0f, halfExtent}, {0, 1, 0}},
  };
  m.indices = {0, 1, 2, 0, 2, 3};
  return m;
}

static MeshCPU makeCirclePN(uint32_t seg, float radius = 0.5f) {
  if (seg < 3)
    seg = 3;

  MeshCPU m{};
  m.vertices.reserve(seg + 1);
  m.indices.reserve(seg * 3);

  // center
  m.vertices.push_back({{0, 0, 0}, {0, 1, 0}});

  const float step = glm::two_pi<float>() / float(seg);
  for (uint32_t i = 0; i < seg; ++i) {
    float a = step * float(i);
    float x = std::cos(a) * radius;
    float z = std::sin(a) * radius;
    m.vertices.push_back({{x, 0, z}, {0, 1, 0}});
  }

  for (uint32_t i = 0; i < seg; ++i) {
    uint32_t a = 0;
    uint32_t b = 1 + i;
    uint32_t c = 1 + ((i + 1) % seg);
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
  }

  return m;
}

static MeshCPU makeSpherePN(uint32_t segU, uint32_t segV, float radius = 0.5f) {
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

      m.vertices.push_back({p, glm::normalize(n)});
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

  return m;
}

MeshCPU makePrimitivePN(ProcMeshType type, uint32_t detail) {
  switch (type) {
  case ProcMeshType::Cube:
    return makeCubePN({.halfExtent = 0.5f});
  case ProcMeshType::Plane:
    return makePlanePN(0.5f);
  case ProcMeshType::Circle:
    return makeCirclePN(detail, 0.5f);
  case ProcMeshType::Sphere:
    return makeSpherePN(detail, detail / 2u, 0.5f);
  case ProcMeshType::Monkey:
    // Placeholder: keep pipeline working; real Suzanne comes soon.
    return makeCubePN({.halfExtent = 0.5f});
  default:
    return makeCubePN({.halfExtent = 0.5f});
  }
}

} // namespace Nyx
