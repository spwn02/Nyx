#pragma once

#include "AssetId.h"
#include "AssetType.h"
#include <string>

namespace Nyx {

struct AssetRecord final {
  AssetId id = 0;
  AssetType type = AssetType::Unknown;

  // Always store relative to project root for portability.
  // Example: "Content/Textures/wood_basecolor.png"
  std::string relPath;

  // Cached UI helpers
  std::string name;   // filename
  std::string folder; // parent folder rel ("" for root)
};

} // namespace Nyx
