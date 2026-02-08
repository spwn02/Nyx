#include "SceneSerializer.h"

#include "scene/Components.h"
#include "scene/EntityUUID.h"
#include "scene/World.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

namespace {

constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

struct EntityRecord {
  EntityID e = InvalidEntity;
  EntityUUID uuid{};
};

struct MaterialRefEntry {
  std::string assetPath; // preferred stable reference
  MaterialHandle legacyHandle = InvalidMaterial;
};

static void writeString(NyxBinaryWriter &w, const std::string &s) {
  w.writeU32(static_cast<uint32_t>(s.size()));
  if (!s.empty())
    w.writeBytes(s.data(), s.size());
}

static std::string readString(NyxBinaryReader &r) {
  const uint32_t len = r.readU32();
  std::string s(len, '\0');
  if (len > 0)
    r.readBytes(s.data(), len);
  return s;
}

static uint32_t internString(std::vector<std::string> &strings,
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

static std::string makeMaterialRefKey(const MeshSubmesh &sm) {
  if (!sm.materialAssetPath.empty())
    return "A:" + sm.materialAssetPath;
  if (sm.material != InvalidMaterial) {
    return "H:" + std::to_string(sm.material.slot) + ":" +
           std::to_string(sm.material.gen);
  }
  return "N:";
}

static void collectSortedEntities(World &world, std::vector<EntityRecord> &out) {
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

  std::sort(out.begin(), out.end(), [](const EntityRecord &a, const EntityRecord &b) {
    return a.uuid.value < b.uuid.value;
  });
}

static void saveStringsAndMap(
    NyxBinaryWriter &w, World &world, const std::vector<EntityRecord> &ents,
    std::vector<std::string> &strings, std::unordered_map<std::string, uint32_t> &stringMap,
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

  for (const auto &cat : world.categories()) {
    internString(strings, stringMap, cat.name);
  }

  w.beginChunk(static_cast<uint32_t>(NyxChunk::STRS), 1);
  w.writeU32(static_cast<uint32_t>(strings.size()));
  for (const std::string &s : strings) {
    writeString(w, s);
  }
  w.endChunk();
}

static void saveEntities(NyxBinaryWriter &w, World &world,
                         const std::vector<EntityRecord> &ents,
                         const std::unordered_map<std::string, uint32_t> &stringMap,
                         std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  entityIndexByRaw.clear();
  entityIndexByRaw.reserve(ents.size());

  for (uint32_t i = 0; i < ents.size(); ++i) {
    entityIndexByRaw.emplace(ents[i].e.index, i);
  }

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

static void saveTransforms(NyxBinaryWriter &w, World &world,
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

static void saveMaterialRefs(NyxBinaryWriter &w,
                             const std::vector<MaterialRefEntry> &materialRefs,
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

static void saveMeshes(NyxBinaryWriter &w, World &world,
                       const std::vector<EntityRecord> &ents,
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

static void saveCameras(NyxBinaryWriter &w, World &world,
                        const std::vector<EntityRecord> &ents,
                        const std::unordered_map<uint32_t, uint32_t> &entityIndexByRaw) {
  w.beginChunk(static_cast<uint32_t>(NyxChunk::CAMR), 2);

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
  }

  w.endChunk();
}

static void saveLights(NyxBinaryWriter &w, World &world,
                       const std::vector<EntityRecord> &ents,
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

static void saveSky(NyxBinaryWriter &w, World &world,
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

static void saveCategories(NyxBinaryWriter &w, World &world,
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

static void loadStrings(NyxBinaryReader &r, const NyxTocEntry &entry,
                        std::vector<std::string> &strings) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  strings.clear();
  strings.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
    strings.push_back(readString(r));
}

static std::string getStringSafe(const std::vector<std::string> &strings,
                                 uint32_t idx, const char *fallback) {
  if (idx < strings.size())
    return strings[idx];
  return fallback;
}

static void loadEntities(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                         const std::vector<std::string> &strings,
                         std::vector<EntityID> &created,
                         std::vector<uint32_t> &parentIndices) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  created.assign(count, InvalidEntity);
  parentIndices.assign(count, kInvalidIndex);

  for (uint32_t i = 0; i < count; ++i) {
    const uint64_t uuidValue = r.readU64();
    const uint32_t nameId = r.readU32();
    const uint32_t parentIdx = r.readU32();
    const uint32_t flags = r.readU32();
    (void)flags;

    EntityID e = world.createEntityWithUUID(EntityUUID{uuidValue}, getStringSafe(strings, nameId, "Entity"));
    created[i] = e;
    parentIndices[i] = parentIdx;
  }

  for (uint32_t i = 0; i < count; ++i) {
    const EntityID child = created[i];
    if (child == InvalidEntity)
      continue;
    const uint32_t parentIdx = parentIndices[i];
    if (parentIdx < created.size()) {
      EntityID parent = created[parentIdx];
      if (parent != InvalidEntity)
        world.setParent(child, parent);
    }
  }
}

static void loadTransforms(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                           const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  for (uint32_t i = 0; i < count && i < created.size(); ++i) {
    const EntityID e = created[i];
    if (e == InvalidEntity)
      continue;

    CTransform &t = world.transform(e);

    t.translation.x = r.readF32();
    t.translation.y = r.readF32();
    t.translation.z = r.readF32();

    t.rotation.x = r.readF32();
    t.rotation.y = r.readF32();
    t.rotation.z = r.readF32();

    if (version >= 2) {
      t.rotation.w = r.readF32();
    } else {
      t.rotation.w = 1.0f;
    }

    t.scale.x = r.readF32();
    t.scale.y = r.readF32();
    t.scale.z = r.readF32();

    if (version >= 2)
      t.hidden = (r.readU8() != 0);

    t.dirty = true;
    world.worldTransform(e).dirty = true;
  }
}

static void loadMaterialRefs(NyxBinaryReader &r, const NyxTocEntry &entry,
                             const std::vector<std::string> &strings,
                             std::vector<MaterialRefEntry> &materialRefs) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;
  const uint32_t count = r.readU32();
  materialRefs.clear();
  materialRefs.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    MaterialRefEntry ref{};
    if (version >= 2) {
      const uint32_t pathID = r.readU32();
      if (pathID < strings.size())
        ref.assetPath = strings[pathID];
      ref.legacyHandle.slot = r.readU32();
      ref.legacyHandle.gen = r.readU32();
    } else {
      ref.legacyHandle.slot = r.readU32();
      ref.legacyHandle.gen = r.readU32();
    }
    materialRefs.push_back(std::move(ref));
  }
}

static void loadMeshes(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                       const std::vector<std::string> &strings,
                       const std::vector<EntityID> &created,
                       const std::vector<MaterialRefEntry> &materialRefs) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t meshEntityCount = r.readU32();
  for (uint32_t i = 0; i < meshEntityCount; ++i) {
    const uint32_t entIdx = r.readU32();
    const uint32_t subCount = r.readU32();

    if (entIdx >= created.size() || created[entIdx] == InvalidEntity) {
      const uint32_t perSubSize = (version >= 2) ? (4u + 1u + 4u) : (4u + 1u + 8u);
      r.skip(uint64_t(subCount) * perSubSize);
      continue;
    }

    EntityID e = created[entIdx];
    CMesh &m = world.ensureMesh(e);
    m.submeshes.clear();
    m.submeshes.resize(subCount);

    for (uint32_t s = 0; s < subCount; ++s) {
      const uint32_t nameId = r.readU32();
      const uint8_t type = r.readU8();

      MaterialRefEntry matRef{};
      if (version >= 2) {
        const uint32_t matRefIdx = r.readU32();
        if (matRefIdx < materialRefs.size())
          matRef = materialRefs[matRefIdx];
      } else {
        matRef.legacyHandle.slot = r.readU32();
        matRef.legacyHandle.gen = r.readU32();
      }

      m.submeshes[s].name = getStringSafe(strings, nameId, "Submesh");
      m.submeshes[s].type = static_cast<ProcMeshType>(type);
      m.submeshes[s].materialAssetPath = matRef.assetPath;
      m.submeshes[s].material = matRef.legacyHandle;
    }
  }
}

static void loadCameras(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                        const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;
  (void)version;

  const uint32_t count = r.readU32();
  const uint32_t activeIndex = r.readU32();

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entIdx = r.readU32();
    if (entIdx >= created.size() || created[entIdx] == InvalidEntity) {
      r.skip(1 + 5 * sizeof(float));
      continue;
    }

    CCamera &c = world.ensureCamera(created[entIdx]);
    c.projection = static_cast<CameraProjection>(r.readU8());
    c.fovYDeg = r.readF32();
    c.orthoHeight = r.readF32();
    c.nearZ = r.readF32();
    c.farZ = r.readF32();
    c.exposure = r.readF32();
    c.dirty = true;
  }

  if (activeIndex < created.size() && created[activeIndex] != InvalidEntity &&
      world.hasCamera(created[activeIndex])) {
    world.setActiveCamera(created[activeIndex]);
  }
}

static void loadLights(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                       const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entIdx = r.readU32();

    if (entIdx >= created.size() || created[entIdx] == InvalidEntity) {
      if (version >= 2) {
        r.skip(1 + 2 + 12 * sizeof(float) + 3 * sizeof(uint32_t));
      } else {
        r.skip(1 + 3 * sizeof(float) + 5 * sizeof(float));
      }
      continue;
    }

    CLight &l = world.ensureLight(created[entIdx]);
    l.type = static_cast<LightType>(r.readU8());

    l.color.x = r.readF32();
    l.color.y = r.readF32();
    l.color.z = r.readF32();

    l.intensity = r.readF32();
    l.radius = r.readF32();
    l.innerAngle = r.readF32();
    l.outerAngle = r.readF32();
    l.exposure = r.readF32();

    if (version >= 2) {
      l.enabled = (r.readU8() != 0);
      l.castShadow = (r.readU8() != 0);

      l.shadowRes = static_cast<uint16_t>(r.readU32());
      l.cascadeRes = static_cast<uint16_t>(r.readU32());
      l.cascadeCount = static_cast<uint8_t>(r.readU32());

      l.normalBias = r.readF32();
      l.slopeBias = r.readF32();
      l.pcfRadius = r.readF32();
      l.pointFar = r.readF32();
    }
  }
}

static void loadSky(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                    const std::vector<std::string> &strings) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  CSky &sky = world.skySettings();

  const uint32_t hdriId = r.readU32();
  sky.hdriPath = (hdriId < strings.size()) ? strings[hdriId] : std::string{};
  sky.intensity = r.readF32();
  sky.exposure = r.readF32();

  if (version >= 2) {
    sky.rotationYawDeg = r.readF32();
    sky.ambient = r.readF32();
    sky.enabled = (r.readU8() != 0);
    sky.drawBackground = (r.readU8() != 0);
  } else {
    sky.rotationYawDeg = 0.0f;
    sky.ambient = 0.03f;
    sky.enabled = true;
    sky.drawBackground = true;
  }
}

static void loadCategories(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                           const std::vector<std::string> &strings,
                           const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;
  (void)version;

  const uint32_t count = r.readU32();
  std::vector<int32_t> parentIndices;
  parentIndices.reserve(count);

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t nameId = r.readU32();
    const int32_t parent = static_cast<int32_t>(r.readU32());
    const uint32_t entityCount = r.readU32();

    const uint32_t catIdx = world.addCategory(getStringSafe(strings, nameId, "Category"));
    (void)catIdx;
    parentIndices.push_back(parent);

    for (uint32_t j = 0; j < entityCount; ++j) {
      const uint32_t entIdx = r.readU32();
      if (entIdx < created.size() && created[entIdx] != InvalidEntity)
        world.addEntityCategory(created[entIdx], static_cast<int32_t>(i));
    }
  }

  for (uint32_t i = 0; i < parentIndices.size(); ++i)
    world.setCategoryParent(i, parentIndices[i]);
}

} // namespace

static bool saveImpl(const std::string &path, World &world) {
  NyxBinaryWriter w(path);
  if (!w.ok())
    return false;

  w.writeU64(NYXSCENE_MAGIC);
  w.writeU32(NYXSCENE_VERSION);

  std::vector<EntityRecord> ents;
  collectSortedEntities(world, ents);

  std::vector<std::string> strings;
  std::unordered_map<std::string, uint32_t> stringMap;
  std::unordered_map<std::string, uint32_t> materialRefMap;
  std::vector<MaterialRefEntry> materialRefs;

  std::unordered_map<uint32_t, uint32_t> entityIndexByRaw;

  saveStringsAndMap(w, world, ents, strings, stringMap, materialRefMap, materialRefs);
  saveEntities(w, world, ents, stringMap, entityIndexByRaw);
  saveTransforms(w, world, ents);
  saveMaterialRefs(w, materialRefs, stringMap);
  saveMeshes(w, world, ents, stringMap, materialRefMap, entityIndexByRaw);
  saveCameras(w, world, ents, entityIndexByRaw);
  saveLights(w, world, ents, entityIndexByRaw);
  saveSky(w, world, stringMap);
  saveCategories(w, world, stringMap, entityIndexByRaw);

  w.finalize();
  return true;
}

static bool loadImpl(const std::string &path, World &world) {
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
  std::vector<MaterialRefEntry> materialRefs;

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::STRS)); entry)
    loadStrings(r, *entry, strings);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::ENTS)); entry)
    loadEntities(r, *entry, world, strings, created, parentIndices);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::TRNS)); entry)
    loadTransforms(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::MATL)); entry)
    loadMaterialRefs(r, *entry, strings, materialRefs);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::MESH)); entry)
    loadMeshes(r, *entry, world, strings, created, materialRefs);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::CAMR)); entry)
    loadCameras(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::LITE)); entry)
    loadLights(r, *entry, world, created);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::SKY)); entry)
    loadSky(r, *entry, world, strings);

  if (auto entry = r.findChunk(static_cast<uint32_t>(NyxChunk::CATS)); entry)
    loadCategories(r, *entry, world, strings, created);

  world.updateTransforms();
  world.clearEvents();
  return true;
}

bool SceneSerializer::save(const std::string &path, World &world) {
  return saveImpl(path, world);
}

bool SceneSerializer::load(const std::string &path, World &world) {
  return loadImpl(path, world);
}

} // namespace Nyx
