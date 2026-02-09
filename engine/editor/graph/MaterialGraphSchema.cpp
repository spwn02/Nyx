#include "editor/graph/MaterialGraphSchema.h"

namespace Nyx {

const std::vector<MaterialNodeDesc> &materialNodePalette() {
  static const std::vector<MaterialNodeDesc> kPalette = {
      {MatNodeType::UV0, "UV0", "Input"},
      {MatNodeType::NormalWS, "NormalWS", "Input"},
      {MatNodeType::ConstFloat, "Float", "Constants"},
      {MatNodeType::ConstVec3, "Vec3", "Constants"},
      {MatNodeType::ConstColor, "Color", "Constants"},
      {MatNodeType::ConstVec4, "Vec4", "Constants"},
      {MatNodeType::Texture2D, "Texture2D", "Textures"},
      {MatNodeType::TextureMRA, "Texture MRA", "Textures"},
      {MatNodeType::NormalMap, "Normal Map", "Textures"},
      {MatNodeType::Add, "Add", "Math"},
      {MatNodeType::Sub, "Sub", "Math"},
      {MatNodeType::Mul, "Mul", "Math"},
      {MatNodeType::Div, "Div", "Math"},
      {MatNodeType::Clamp01, "Clamp01", "Math"},
      {MatNodeType::OneMinus, "OneMinus", "Math"},
      {MatNodeType::Lerp, "Lerp", "Math"},
      {MatNodeType::SurfaceOutput, "Surface Output", "Output"},
  };
  return kPalette;
}

const MaterialNodeDesc *findMaterialNodeDesc(MatNodeType type) {
  const std::vector<MaterialNodeDesc> &nodes = materialNodePalette();
  for (const MaterialNodeDesc &n : nodes) {
    if (n.type == type)
      return &n;
  }
  return nullptr;
}

const char *materialNodeName(MatNodeType type) {
  if (const MaterialNodeDesc *d = findMaterialNodeDesc(type))
    return d->name;
  return "Node";
}

uint32_t materialInputCount(MatNodeType type) {
  switch (type) {
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
    return 1;
  case MatNodeType::NormalMap:
    return 3;
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
    return 2;
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
    return 1;
  case MatNodeType::Lerp:
    return 3;
  case MatNodeType::SurfaceOutput:
    return 7;
  default:
    return 0;
  }
}

uint32_t materialOutputCount(MatNodeType type) {
  switch (type) {
  case MatNodeType::SurfaceOutput:
    return 0;
  case MatNodeType::ConstFloat:
  case MatNodeType::ConstVec3:
  case MatNodeType::ConstColor:
  case MatNodeType::ConstVec4:
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
  case MatNodeType::NormalMap:
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
  case MatNodeType::Lerp:
  case MatNodeType::UV0:
  case MatNodeType::NormalWS:
    return 1;
  default:
    return 1;
  }
}

const char *materialInputName(MatNodeType type, uint32_t slot) {
  switch (type) {
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
    return (slot == 0) ? "UV" : "";
  case MatNodeType::NormalMap:
    if (slot == 0)
      return "UV";
    if (slot == 1)
      return "NormalWS";
    if (slot == 2)
      return "Strength";
    return "";
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
    return (slot == 0) ? "A" : "B";
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
    return "In";
  case MatNodeType::Lerp:
    return slot == 0 ? "A" : (slot == 1 ? "B" : "T");
  case MatNodeType::SurfaceOutput:
    switch (slot) {
    case 0:
      return "BaseColor";
    case 1:
      return "Metallic";
    case 2:
      return "Roughness";
    case 3:
      return "NormalWS";
    case 4:
      return "AO";
    case 5:
      return "Emissive";
    case 6:
      return "Alpha";
    default:
      return "";
    }
  default:
    return "";
  }
}

const char *materialOutputName(MatNodeType type, uint32_t slot) {
  (void)slot;
  switch (type) {
  case MatNodeType::ConstFloat:
    return "F";
  case MatNodeType::ConstVec3:
  case MatNodeType::ConstColor:
    return "RGB";
  case MatNodeType::ConstVec4:
  case MatNodeType::Texture2D:
    return "RGBA";
  case MatNodeType::TextureMRA:
    return "MRA";
  case MatNodeType::Swizzle:
    return "Out";
  case MatNodeType::Channel:
    return "Ch";
  case MatNodeType::Split:
    if (slot == 0)
      return "X";
    if (slot == 1)
      return "Y";
    if (slot == 2)
      return "Z";
    if (slot == 3)
      return "W";
    return "Out";
  case MatNodeType::NormalMap:
    return "Normal";
  case MatNodeType::UV0:
    return "UV";
  case MatNodeType::NormalWS:
    return "Normal";
  case MatNodeType::ViewDirWS:
    return "ViewDir";
  default:
    return "Out";
  }
}

} // namespace Nyx
