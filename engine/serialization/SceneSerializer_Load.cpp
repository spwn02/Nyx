#include "SceneSerializer_Impl.h"

#include "NyxChunkIDs.h"
#include "NyxBinaryReader.h"
#include "SceneSerializer_ChunkIO.h"
#include "scene/World.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx::detail {

bool loadSceneBinary(const std::string &path, World &world) {
  NyxBinaryReader r(path);
  if (!r.ok())
    return false;

  uint64_t magic = 0;
  uint32_t version = 0;
  if (!r.readSceneHeader(magic, version))
    return false;

  if (magic != NYXSCENE_MAGIC)
    return false;

  const uint32_t fileMajor = version & 0xFFFF0000u;
  const uint32_t localMajor = NYXSCENE_VERSION & 0xFFFF0000u;
  if (fileMajor != localMajor)
    return false;

  if (!r.loadTOC())
    return false;

  world.clear();

  std::vector<std::string> strings;
  std::vector<EntityID> created;
  std::vector<uint32_t> parentIndices;
  std::vector<sceneio::MaterialRefEntry> materialRefs;

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::STRS)); entry)
    sceneio::loadStrings(r, *entry, strings);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::ENTS)); entry)
    sceneio::loadEntities(r, *entry, world, strings, created, parentIndices);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::TRNS)); entry)
    sceneio::loadTransforms(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::MATL)); entry)
    sceneio::loadMaterialRefs(r, *entry, strings, materialRefs);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::MESH)); entry)
    sceneio::loadMeshes(r, *entry, world, strings, created, materialRefs);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::CAMR)); entry)
    sceneio::loadCameras(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::LITE)); entry)
    sceneio::loadLights(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::SKY)); entry)
    sceneio::loadSky(r, *entry, world, strings);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::CATS)); entry)
    sceneio::loadCategories(r, *entry, world, strings, created);

  world.updateTransforms();
  world.clearEvents();
  return true;
}

} // namespace Nyx::detail
