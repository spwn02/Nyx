#pragma once

#include "NyxBinaryReader.h"
#include "NyxBinaryWriter.h"
#include "scene/Components.h"
#include "scene/EntityID.h"
#include "scene/EntityUUID.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

class World;

namespace detail::sceneio {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

struct EntityRecord {
  EntityID e = InvalidEntity;
  EntityUUID uuid{};
};

struct MaterialRefEntry {
  std::string assetPath;
  MaterialHandle legacyHandle = InvalidMaterial;
};

void collectSortedEntities(World &world, std::vector<EntityRecord> &out);

void saveStringsAndMap(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    std::vector<std::string> &strings,
    std::unordered_map<std::string, uint32_t> &stringMap,
    std::unordered_map<std::string, uint32_t> &materialRefMap,
    std::vector<MaterialRefEntry> &materialRefs);

void saveEntities(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw);

void saveTransforms(NyxBinaryWriter &w, World &world,
                    const std::vector<EntityRecord> &ents);

void saveMaterialRefs(
    NyxBinaryWriter &w, const std::vector<MaterialRefEntry> &materialRefs,
    const std::unordered_map<std::string, uint32_t> &stringMap);

void saveMeshes(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    const std::unordered_map<std::string, uint32_t> &materialRefMap,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw);

void saveCameras(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw);

void saveLights(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw);

void saveSky(NyxBinaryWriter &w, World &world,
             const std::unordered_map<std::string, uint32_t> &stringMap);

void saveCategories(
    NyxBinaryWriter &w, World &world,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw);

void loadStrings(NyxBinaryReader &r, const NyxTocEntry &entry,
                 std::vector<std::string> &strings);

void loadEntities(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                  const std::vector<std::string> &strings,
                  std::vector<EntityID> &created,
                  std::vector<uint32_t> &parentIndices);

void loadTransforms(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                    const std::vector<EntityID> &created);

void loadMaterialRefs(NyxBinaryReader &r, const NyxTocEntry &entry,
                      const std::vector<std::string> &strings,
                      std::vector<MaterialRefEntry> &materialRefs);

void loadMeshes(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                const std::vector<std::string> &strings,
                const std::vector<EntityID> &created,
                const std::vector<MaterialRefEntry> &materialRefs);

void loadCameras(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                 const std::vector<EntityID> &created);

void loadLights(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                const std::vector<EntityID> &created);

void loadSky(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
             const std::vector<std::string> &strings);

void loadCategories(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                    const std::vector<std::string> &strings,
                    const std::vector<EntityID> &created);

} // namespace detail::sceneio

} // namespace Nyx
