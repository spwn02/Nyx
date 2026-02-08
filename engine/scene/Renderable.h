#pragma once

#include "EntityID.h"
#include "scene/Components.h"
#include "render/material/MaterialGraph.h"
#include <glm/glm.hpp>

namespace Nyx {

struct Renderable {
  EntityID entity = InvalidEntity;
  uint32_t submesh = 0;

  ProcMeshType mesh = ProcMeshType::Cube;
  glm::mat4 model{1.0f};

  uint32_t pickID = 0; // packed entity + submesh
  uint32_t materialGpuIndex = 0; // index into material SSBO

  MatAlphaMode alphaMode = MatAlphaMode::Opaque;
  float sortKey = 0.0f; // used for transparent sorting

  bool isLight = false;
  bool isCamera = false;
  glm::vec3 lightColor{1.0f};
  float lightIntensity = 1.0f;
  float lightExposure = 0.0f;
};

} // namespace Nyx
