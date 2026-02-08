#pragma once

#include "scene/material/MaterialTypes.h"
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Nyx {

// A "node editor" node id
using MatNodeID = uint32_t;

enum class MatNodeType : uint32_t {
  // inputs
  UV0,
  NormalWS,
  ViewDirWS,

  // const
  ConstFloat,
  ConstVec3,
  ConstColor,
  ConstVec4,

  // textures
  Texture2D,  // generic (linear or sRGB flag)
  TextureMRA, // packed M/R/AO
  NormalMap,  // tangent-space normal

  // math
  Add,
  Sub,
  Mul,
  Div,
  Min,
  Max,
  Clamp01,
  OneMinus,
  Lerp,
  Pow,
  Dot3,
  Normalize3,

  // wiring
  Swizzle,
  Split,
  Channel,
  Append,

  // output
  SurfaceOutput,
};

struct MatPin final {
  MatNodeID node = 0;
  uint32_t slot = 0; // pin index on that node
};

struct MatLink final {
  uint64_t id = 0;
  MatPin from;
  MatPin to;
};

struct MatNode final {
  MatNodeID id = 0;
  MatNodeType type{MatNodeType::ConstFloat};

  // parameters (editor-side)
  glm::vec4 f{0};  // generic numeric params
  glm::uvec4 u{0}; // generic ids/flags/tex indices etc.
  std::string label;
  std::string path; // optional asset path (UI)
  glm::vec2 pos{0.0f};
  bool posSet = false;

  // per-node input pins: "to slot i" is satisfied by a link
};

struct MaterialGraph final {
  uint32_t nextNodeId = 1;
  uint64_t nextLinkId = 1;
  std::vector<MatNode> nodes;
  std::vector<MatLink> links;

  // material-level settings
  MatAlphaMode alphaMode{MatAlphaMode::Opaque};
  float alphaCutoff = 0.5f;

  // convenience: find output node
  MatNodeID findSurfaceOutput() const {
    for (auto &n : nodes)
      if (n.type == MatNodeType::SurfaceOutput)
        return n.id;
    return 0;
  }
};

} // namespace Nyx
