#include "SceneSerializer_Impl.h"

#include "NyxChunkIDs.h"
#include "NyxBinaryWriter.h"
#include "SceneSerializer_ChunkIO.h"
#include "scene/World.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx::detail {

bool saveSceneBinary(const std::string &path, World &world) {
  NyxBinaryWriter w(path);
  if (!w.ok())
    return false;

  w.writeU64(NYXSCENE_MAGIC);
  w.writeU32(NYXSCENE_VERSION);

  std::vector<sceneio::EntityRecord> ents;
  sceneio::collectSortedEntities(world, ents);

  std::vector<std::string> strings;
  std::unordered_map<std::string, uint32_t> stringMap;
  std::unordered_map<std::string, uint32_t> materialRefMap;
  std::vector<sceneio::MaterialRefEntry> materialRefs;
  std::unordered_map<uint32_t, uint32_t> entityIndexByRaw;

  sceneio::saveStringsAndMap(w, world, ents, strings, stringMap, materialRefMap,
                             materialRefs);
  sceneio::saveEntities(w, world, ents, stringMap, entityIndexByRaw);
  sceneio::saveTransforms(w, world, ents);
  sceneio::saveMaterialRefs(w, materialRefs, stringMap);
  sceneio::saveMeshes(w, world, ents, stringMap, materialRefMap,
                      entityIndexByRaw);
  sceneio::saveCameras(w, world, ents, entityIndexByRaw);
  sceneio::saveLights(w, world, ents, entityIndexByRaw);
  sceneio::saveSky(w, world, stringMap);
  sceneio::saveCategories(w, world, stringMap, entityIndexByRaw);

  w.finalize();
  return true;
}

} // namespace Nyx::detail
