#pragma once

#include "MeshCPU.h"

namespace Nyx {

struct CubeDesc {
  float halfExtent = 0.5f;
};

MeshCPU makeCubePN(const CubeDesc &d);

} // namespace Nyx
