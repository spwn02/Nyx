#include "SceneSerializer_ChunkIO.h"

#include "NyxChunkIDs.h"
#include "scene/Components.h"
#include "scene/World.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx::detail::sceneio {

namespace {

void writeString(NyxBinaryWriter &w, const std::string &s) {
  w.writeU32(static_cast<uint32_t>(s.size()));
  if (!s.empty())
    w.writeBytes(s.data(), s.size());
}

uint32_t internString(std::vector<std::string> &strings,
                      std::unordered_map<std::string, uint32_t> &map,
                      const std::string &s) {
  auto it = map.find(s);
  if (it != map.end())
    return it->second;
  const uint32_t id = static_cast<uint32_t>(strings.size());
  strings.push_back(s);
  map.emplace(s, id);
  return id;
}

std::string makeMaterialRefKey(const MeshSubmesh &sm) {
  if (!sm.materialAssetPath.empty())
    return "A:" + sm.materialAssetPath;
  if (sm.material != InvalidMaterial) {
    return "H:" + std::to_string(sm.material.slot) + ":" +
           std::to_string(sm.material.gen);
  }
  return "N:";
}

} // namespace

void collectSortedEntities(World &world, std::vector<EntityRecord> &out) {
  out.clear();
  out.reserve(world.alive().size());
  for (EntityID e : world.alive()) {
    if (!world.isAlive(e))
      continue;
    const EntityUUID id = world.uuid(e);
    if (!id)
      continue;
    out.push_back({e, id});
  }

  std::sort(out.begin(), out.end(),
            [](const EntityRecord &a, const EntityRecord &b) {
              return a.uuid.value < b.uuid.value;
            });
}

void saveStringsAndMap(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    std::vector<std::string> &strings,
    std::unordered_map<std::string, uint32_t> &stringMap,
    std::unordered_map<std::string, uint32_t> &materialRefMap,
    std::vector<MaterialRefEntry> &materialRefs) {
  strings.clear();
  stringMap.clear();
  materialRefMap.clear();
  materialRefs.clear();

  for (const EntityRecord &rec : ents) {
    const EntityID e = rec.e;
    internString(strings, stringMap, world.name(e).name);

    if (world.hasMesh(e)) {
      const CMesh &m = world.mesh(e);
      for (const MeshSubmesh &sm : m.submeshes) {
        internString(strings, stringMap, sm.name);

        if (!sm.materialAssetPath.empty())
          internString(strings, stringMap, sm.materialAssetPath);

        const std::string key = makeMaterialRefKey(sm);
        if (materialRefMap.find(key) == materialRefMap.end()) {
          const uint32_t idx = static_cast<uint32_t>(materialRefs.size());
          materialRefMap.emplace(key, idx);
          MaterialRefEntry entry{};
          entry.assetPath = sm.materialAssetPath;
          entry.legacyHandle = sm.material;
          materialRefs.push_back(std::move(entry));
        }
      }
    }
  }

  const CSky &sky = world.skySettings();
  if (!sky.hdriPath.empty())
    internString(strings, stringMap, sky.hdriPath);

  for (const auto &cat : world.categories())
    internString(strings, stringMap, cat.name);

  w.beginChunk(static_cast<uint32_t>(NyxChunk::STRS), 1);
  w.writeU32(static_cast<uint32_t>(strings.size()));
  for (const std::string &s : strings)
    writeString(w, s);
  w.endChunk();
}

void saveEntities(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  entityIndexByRaw.clear();
  entityIndexByRaw.reserve(ents.size());

  for (uint32_t i = 0; i < ents.size(); ++i)
    entityIndexByRaw.emplace(ents[i].e.index, i);

  w.beginChunk(static_cast<uint32_t>(NyxChunk::ENTS), 1);
  w.writeU32(static_cast<uint32_t>(ents.size()));

  for (uint32_t i = 0; i < ents.size(); ++i) {
    const EntityID e = ents[i].e;
    const EntityUUID id = world.uuid(e);

    w.writeU64(id.value);

    const std::string &name = world.name(e).name;
    auto nit = stringMap.find(name);
    w.writeU32(nit == stringMap.end() ? 0u : nit->second);

    EntityID parent = world.parentOf(e);
    uint32_t parentIndex = kInvalidIndex;
    if (parent != InvalidEntity) {
      auto pit = entityIndexByRaw.find(parent.index);
      if (pit != entityIndexByRaw.end())
        parentIndex = pit->second;
    }
    w.writeU32(parentIndex);

    uint32_t flags = 0;
    if (world.hasMesh(e))
      flags |= 1u << 0;
    if (world.hasLight(e))
      flags |= 1u << 1;
    if (world.hasCamera(e))
      flags |= 1u << 2;
    w.writeU32(flags);
  }

  w.endChunk();
}

void saveTransforms(NyxBinaryWriter &w, World &world,
                    const std::vector<EntityRecord> &ents) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::TRNS), 2);
  w.writeU32(static_cast<uint32_t>(ents.size()));

  for (const EntityRecord &rec : ents) {
    const CTransform &t = world.transform(rec.e);

    w.writeF32(t.translation.x);
    w.writeF32(t.translation.y);
    w.writeF32(t.translation.z);

    w.writeF32(t.rotation.x);
    w.writeF32(t.rotation.y);
    w.writeF32(t.rotation.z);
    w.writeF32(t.rotation.w);

    w.writeF32(t.scale.x);
    w.writeF32(t.scale.y);
    w.writeF32(t.scale.z);

    w.writeU8(t.hidden ? 1u : 0u);
  }

  w.endChunk();
}

void saveMaterialRefs(
    NyxBinaryWriter &w, const std::vector<MaterialRefEntry> &materialRefs,
    const std::unordered_map<std::string, uint32_t> &stringMap) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::MATL), 2);
  w.writeU32(static_cast<uint32_t>(materialRefs.size()));
  for (const MaterialRefEntry &ref : materialRefs) {
    uint32_t pathID = kInvalidIndex;
    if (!ref.assetPath.empty()) {
      auto it = stringMap.find(ref.assetPath);
      if (it != stringMap.end())
        pathID = it->second;
    }
    w.writeU32(pathID);
    w.writeU32(ref.legacyHandle.slot);
    w.writeU32(ref.legacyHandle.gen);
  }
  w.endChunk();
}

void saveMeshes(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    const std::unordered_map<std::string, uint32_t> &materialRefMap,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::MESH), 2);

  std::vector<EntityID> meshEnts;
  meshEnts.reserve(ents.size());
  for (const EntityRecord &rec : ents) {
    if (world.hasMesh(rec.e))
      meshEnts.push_back(rec.e);
  }

  w.writeU32(static_cast<uint32_t>(meshEnts.size()));

  for (EntityID e : meshEnts) {
    auto eit = entityIndexByRaw.find(e.index);
    if (eit == entityIndexByRaw.end())
      continue;

    w.writeU32(eit->second);

    const CMesh &m = world.mesh(e);
    w.writeU32(static_cast<uint32_t>(m.submeshes.size()));

    for (const MeshSubmesh &sm : m.submeshes) {
      auto sit = stringMap.find(sm.name);
      w.writeU32(sit == stringMap.end() ? 0u : sit->second);
      w.writeU8(static_cast<uint8_t>(sm.type));

      uint32_t materialIdx = kInvalidIndex;
      const std::string key = makeMaterialRefKey(sm);
      auto mit = materialRefMap.find(key);
      if (mit != materialRefMap.end())
        materialIdx = mit->second;
      w.writeU32(materialIdx);
    }
  }

  w.endChunk();
}

void saveCameras(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::CAMR), 3);

  std::vector<EntityID> cams;
  cams.reserve(ents.size());
  for (const EntityRecord &rec : ents) {
    if (world.hasCamera(rec.e))
      cams.push_back(rec.e);
  }

  w.writeU32(static_cast<uint32_t>(cams.size()));

  uint32_t activeIndex = kInvalidIndex;
  const EntityID active = world.activeCamera();
  if (active != InvalidEntity) {
    auto ait = entityIndexByRaw.find(active.index);
    if (ait != entityIndexByRaw.end())
      activeIndex = ait->second;
  }
  w.writeU32(activeIndex);

  for (EntityID e : cams) {
    auto eit = entityIndexByRaw.find(e.index);
    if (eit == entityIndexByRaw.end())
      continue;

    const CCamera &c = world.camera(e);
    w.writeU32(eit->second);
    w.writeU8(static_cast<uint8_t>(c.projection));
    w.writeF32(c.fovYDeg);
    w.writeF32(c.orthoHeight);
    w.writeF32(c.nearZ);
    w.writeF32(c.farZ);
    w.writeF32(c.exposure);
    w.writeF32(c.aperture);
    w.writeF32(c.focusDistance);
    w.writeF32(c.sensorWidth);
    w.writeF32(c.sensorHeight);
  }

  w.endChunk();
}

void saveLights(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::LITE), 2);

  std::vector<EntityID> lights;
  lights.reserve(ents.size());
  for (const EntityRecord &rec : ents) {
    if (world.hasLight(rec.e))
      lights.push_back(rec.e);
  }

  w.writeU32(static_cast<uint32_t>(lights.size()));

  for (EntityID e : lights) {
    auto eit = entityIndexByRaw.find(e.index);
    if (eit == entityIndexByRaw.end())
      continue;

    const CLight &l = world.light(e);
    w.writeU32(eit->second);
    w.writeU8(static_cast<uint8_t>(l.type));

    w.writeF32(l.color.x);
    w.writeF32(l.color.y);
    w.writeF32(l.color.z);

    w.writeF32(l.intensity);
    w.writeF32(l.radius);
    w.writeF32(l.innerAngle);
    w.writeF32(l.outerAngle);
    w.writeF32(l.exposure);

    w.writeU8(l.enabled ? 1u : 0u);
    w.writeU8(l.castShadow ? 1u : 0u);

    w.writeU32(static_cast<uint32_t>(l.shadowRes));
    w.writeU32(static_cast<uint32_t>(l.cascadeRes));
    w.writeU32(static_cast<uint32_t>(l.cascadeCount));

    w.writeF32(l.normalBias);
    w.writeF32(l.slopeBias);
    w.writeF32(l.pcfRadius);
    w.writeF32(l.pointFar);
  }

  w.endChunk();
}

void saveSky(NyxBinaryWriter &w, World &world,
             const std::unordered_map<std::string, uint32_t> &stringMap) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::SKY), 2);

  const CSky &sky = world.skySettings();
  uint32_t hdriId = 0;
  auto it = stringMap.find(sky.hdriPath);
  if (it != stringMap.end())
    hdriId = it->second;

  w.writeU32(hdriId);
  w.writeF32(sky.intensity);
  w.writeF32(sky.exposure);
  w.writeF32(sky.rotationYawDeg);
  w.writeF32(sky.ambient);
  w.writeU8(sky.enabled ? 1u : 0u);
  w.writeU8(sky.drawBackground ? 1u : 0u);

  w.endChunk();
}

void saveCategories(
    NyxBinaryWriter &w, World &world,
    const std::unordered_map<std::string, uint32_t> &stringMap,
    const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  const auto &cats = world.categories();
  if (cats.empty())
    return;

  w.beginChunk(static_cast<uint32_t>(NyxChunk::CATS), 1);
  w.writeU32(static_cast<uint32_t>(cats.size()));

  for (const auto &cat : cats) {
    auto nit = stringMap.find(cat.name);
    w.writeU32(nit == stringMap.end() ? 0u : nit->second);
    w.writeU32(static_cast<uint32_t>(cat.parent));

    std::vector<uint32_t> members;
    members.reserve(cat.entities.size());
    for (EntityID e : cat.entities) {
      auto eit = entityIndexByRaw.find(e.index);
      if (eit != entityIndexByRaw.end())
        members.push_back(eit->second);
    }

    w.writeU32(static_cast<uint32_t>(members.size()));
    for (uint32_t idx : members)
      w.writeU32(idx);
  }

  w.endChunk();
}

} // namespace Nyx::detail::sceneio
