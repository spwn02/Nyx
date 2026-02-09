#pragma once

#include "render/material/MaterialGraph.h"

#include <cstdint>
#include <vector>

namespace Nyx {

struct MaterialNodeDesc final {
  MatNodeType type;
  const char *name;
  const char *category;
};

const std::vector<MaterialNodeDesc> &materialNodePalette();
const MaterialNodeDesc *findMaterialNodeDesc(MatNodeType type);

const char *materialNodeName(MatNodeType type);
uint32_t materialInputCount(MatNodeType type);
uint32_t materialOutputCount(MatNodeType type);
const char *materialInputName(MatNodeType type, uint32_t slot);
const char *materialOutputName(MatNodeType type, uint32_t slot);

} // namespace Nyx
