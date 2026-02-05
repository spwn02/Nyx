#include "MeshImporterGLTF.h"
#include "npgms/MeshCPU.h"
#include "npgms/MikkTangentBuilder.h"

namespace Nyx {

static void finalizeMeshTangents(MeshCPU &m) {
  m.hasTangents = Nyx::Tangents::buildTangents_Mikk(m);
}

std::expected<MeshAssetCPU, std::string>
MeshImporterGLTF::importFile(const std::string &path) {
  (void)path;
  return std::unexpected("GLTF import not implemented yet.");
}

} // namespace Nyx
