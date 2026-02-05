#pragma once

#include "../material/MaterialHandle.h"
#include "scene/EntityID.h"
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <vector>

namespace Nyx {

// Hierarchy storage: sibling-linked tree
struct CHierarchy final {
  // Tree links
  // - parent: InvalidEntity if root
  // - firstChild: first child entity
  // - nextSibling: next child under same parent
  // - prevSibling: optional but makes detach O(1)
  // - lastChild: optional, enables appendChild O(1)
  EntityID parent = InvalidEntity;
  EntityID firstChild = InvalidEntity;
  EntityID nextSibling = InvalidEntity;
};

struct CName final {
  std::string name;
};

struct CTransform final {
  glm::vec3 translation{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // w,x,y,z
  glm::vec3 scale{1.0f};

  bool dirty = true;
};

struct CWorldTransform final {
  glm::mat4 world{1.0f};
  bool dirty = true;
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

// Phase 2: unified light component (you said you already generalized it)
enum class LightType : uint8_t {
  Directional = 0,
  Point,
  Spot,
};

struct CLight final {
  LightType type = LightType::Point;

  glm::vec3 color{1.0f};
  float intensity = 10.0f; // “watts-ish” / artistic

  // Point/Spot: radius controls attenuation range
  float radius = 5.0f;

  // Spot: inner/outer cone (radians)
  float innerAngle = glm::radians(15.0f);
  float outerAngle = glm::radians(25.0f);

  // “exposure-ish” multiplier for artistic control
  float exposure = 0.0f;

  bool enabled = true;
  // Shadow parameters
  bool castShadow = true;
  uint16_t shadowRes = 1024;      // Resolution for spot/point shadows
  uint16_t cascadeRes = 1024;     // Resolution per cascade (directional)
  uint8_t cascadeCount = 4;       // Number of CSM cascades (directional)
  float normalBias = 0.0025f;     // Normal-based bias
  float slopeBias = 1.0f;         // Slope-based bias
  float pcfRadius = 2.0f;         // PCF filter radius in texels
  float pointFar = 25.0f;         // Far plane for point light shadows
};

struct CSky final {
  std::string hdriPath;       // Path to HDRI equirect (EXR/HDR)
  float intensity = 1.0f;     // Multiplier for sky/IBL
  float exposure = 0.0f;      // Stops (pow(2, exposure))
  float rotationYawDeg = 0.0f; // Rotation around Y axis
  float ambient = 0.03f;      // Fallback ambient when no IBL
  bool enabled = true;
  bool drawBackground = true; // Sky visible in viewport
};

} // namespace Nyx
