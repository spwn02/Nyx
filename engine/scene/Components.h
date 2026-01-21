#pragma once

#include "EntityID.h"
#include "material/MaterialHandle.h"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <vector>

namespace Nyx {

// Hierarchy storage: sibling-linked tree
// - parent points to parent entity (or InvalidEntity if root)
// - firstChild points to first child
// - nextSibling creates a forward linked list under the same parent
struct CHierarchy final {
  EntityID parent = InvalidEntity;
  EntityID firstChild = InvalidEntity;
  EntityID nextSibling = InvalidEntity;
};

struct CName final {
  std::string name;
};

struct CTransform final {
  glm::vec3 translation{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w, x, y, z
  glm::vec3 scale{1.0f};

  bool dirty = true; // local changed
};

struct CWorldTransform final {
  glm::mat4 world{1.0f};
  bool dirty = true; // needs recompute
};

enum class ProcMeshType : uint8_t {
  Cube = 0,
  Plane,
  Circle,
  Sphere,
  Monkey,
};

struct MeshSubmesh final {
  std::string name{"Submesh 0"};
  ProcMeshType type = ProcMeshType::Cube;
  MaterialHandle material = InvalidMaterial;
};

struct CMesh final {
  std::vector<MeshSubmesh> submeshes;
};

} // namespace Nyx
