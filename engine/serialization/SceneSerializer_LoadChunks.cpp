#include "SceneSerializer_ChunkIO.h"

#include "scene/Components.h"
#include "scene/World.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx::detail::sceneio {

namespace {

std::string readString(NyxBinaryReader &r) {
  const uint32_t len = r.readU32();
  std::string s(len, '\0');
  if (len > 0)
    r.readBytes(s.data(), len);
  return s;
}

std::string getStringSafe(const std::vector<std::string> &strings,
                          uint32_t idx, const char *fallback) {
  if (idx < strings.size())
    return strings[idx];
  return fallback;
}

} // namespace

void loadStrings(NyxBinaryReader &r, const NyxTocEntry &entry,
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

void loadEntities(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
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

    EntityID e = world.createEntityWithUUID(
        EntityUUID{uuidValue}, getStringSafe(strings, nameId, "Entity"));
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

void loadTransforms(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
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

void loadMaterialRefs(NyxBinaryReader &r, const NyxTocEntry &entry,
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

void loadMeshes(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
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

void loadCameras(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                 const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  const uint32_t activeIndex = r.readU32();

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t entIdx = r.readU32();
    if (entIdx >= created.size() || created[entIdx] == InvalidEntity) {
      if (version >= 3) {
        r.skip(1 + 9 * sizeof(float));
      } else {
        r.skip(1 + 5 * sizeof(float));
      }
      continue;
    }

    CCamera &c = world.ensureCamera(created[entIdx]);
    c.projection = static_cast<CameraProjection>(r.readU8());
    c.fovYDeg = r.readF32();
    c.orthoHeight = r.readF32();
    c.nearZ = r.readF32();
    c.farZ = r.readF32();
    c.exposure = r.readF32();
    if (version >= 3) {
      c.aperture = r.readF32();
      c.focusDistance = r.readF32();
      c.sensorWidth = r.readF32();
      c.sensorHeight = r.readF32();
    }
    c.dirty = true;
  }

  if (activeIndex < created.size() && created[activeIndex] != InvalidEntity &&
      world.hasCamera(created[activeIndex])) {
    world.setActiveCamera(created[activeIndex]);
  }
}

void loadLights(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
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

void loadSky(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
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

void loadCategories(NyxBinaryReader &r, const NyxTocEntry &entry, World &world,
                    const std::vector<std::string> &strings,
                    const std::vector<EntityID> &created) {
  r.seek(entry.offset);
  uint32_t fourcc = 0;
  uint32_t version = 0;
  uint64_t size = 0;
  if (!r.readChunkHeader(fourcc, version, size))
    return;

  const uint32_t count = r.readU32();
  std::vector<int32_t> parentIndices;
  parentIndices.reserve(count);

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t nameId = r.readU32();
    const int32_t parent = static_cast<int32_t>(r.readU32());
    const uint32_t entityCount = r.readU32();

    const uint32_t catIdx =
        world.addCategory(getStringSafe(strings, nameId, "Category"));
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

} // namespace Nyx::detail::sceneio
