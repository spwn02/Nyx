#pragma once

#include <cstdint>

namespace Nyx {

enum class AssetType : uint8_t {
  Unknown = 0,
  Folder,

  // Source assets / authored
  Texture2D,
  Mesh,
  MaterialGraph,
  PostGraph,
  AnimationClip,
  SkyEnv,  // HDRI/IBL authoring asset
  Scene,   // .nyxscene
  Project, // .nyxproj
};

inline const char *assetTypeName(AssetType t) {
  switch (t) {
  case AssetType::Folder:
    return "Folder";
  case AssetType::Texture2D:
    return "Texture2D";
  case AssetType::Mesh:
    return "Mesh";
  case AssetType::Scene:
    return "Scene";
  case AssetType::Project:
    return "Project";
  default:
    return "Unknown";
  }
}

} // namespace Nyx
