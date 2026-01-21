#pragma once

#include "../resources/MeshAssetCPU.h"
#include <expected>
#include <string>

namespace Nyx {

class MeshImporterGLTF final {
public:
  static std::expected<MeshAssetCPU, std::string>
  importFile(const std::string &path);
};

} // namespace Nyx
