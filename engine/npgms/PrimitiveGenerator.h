#pragma once
#include "MeshCPU.h"
#include "scene/Components.h"

namespace Nyx {

// detail: sphere segments, circle segments
MeshCPU makePrimitivePN(ProcMeshType type, uint32_t detail = 32);

} // namespace Nyx
