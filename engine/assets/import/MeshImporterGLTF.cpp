#include "MeshImporterGLTF.h"

namespace Nyx {

std::expected<MeshAssetCPU, std::string>
MeshImporterGLTF::importFile(const std::string &path) {
  (void)path;
  return std::unexpected("GLTF import not implemented yet.");
}

} // namespace Nyx
