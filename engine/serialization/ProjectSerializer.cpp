#include "ProjectSerializer.h"
#include "NyxProjectFormat.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Nyx {

namespace {

static constexpr uint32_t kInvalidStr = 0xFFFFFFFFu;

static void writeString(NyxBinaryWriter &w, const std::string &s) {
  w.writeU32(static_cast<uint32_t>(s.size()));
  if (!s.empty())
    w.writeBytes(s.data(), s.size());
}

static std::string readString(NyxBinaryReader &r) {
  uint32_t size = r.readU32();
  std::string s(size, '\0');
  if (size)
    r.readBytes(s.data(), size);
  return s;
}

static uint32_t stringID(std::vector<std::string> &strings,
                         std::unordered_map<std::string, uint32_t> &indices,
                         const std::string &s) {
  auto it = indices.find(s);
  if (it != indices.end())
    return it->second;
  const uint32_t id = static_cast<uint32_t>(strings.size());
  strings.push_back(s);
  indices.emplace(s, id);
  return id;
}

static std::string getStringByID(const std::vector<std::string> &strings,
                                 uint32_t id) {
  if (id == kInvalidStr || id >= strings.size())
    return {};
  return strings[id];
}

} // namespace

bool ProjectSerializer::save(const std::string &path, const NyxProject &p) {
  NyxBinaryWriter w(path);
  if (!w.ok())
    return false;

  w.writeU64(NYXPROJ_MAGIC);
  w.writeU32(NYXPROJ_VERSION);

  std::vector<std::string> strings;
  std::unordered_map<std::string, uint32_t> stringMap;

  const uint32_t nameID = stringID(strings, stringMap, p.name);
  const uint32_t startupID = stringID(strings, stringMap, p.startupScene);
  const uint32_t lastSceneID = stringID(strings, stringMap, p.lastScene);
  for (const auto &m : p.mounts) {
    stringID(strings, stringMap, m.virtualRoot);
    stringID(strings, stringMap, m.diskPath);
  }

  w.beginChunk(static_cast<uint32_t>(NyxProjChunk::HEAD), 1);
  w.writeU32(nameID);
  w.writeU32(lastSceneID);
  w.endChunk();

  w.beginChunk(static_cast<uint32_t>(NyxProjChunk::STRS), 1);
  w.writeU32(static_cast<uint32_t>(strings.size()));
  for (const auto &s : strings)
    writeString(w, s);
  w.endChunk();

  w.beginChunk(static_cast<uint32_t>(NyxProjChunk::MNT), 1);
  w.writeU32(static_cast<uint32_t>(p.mounts.size()));
  for (const AssetMount &m : p.mounts) {
    w.writeU32(stringID(strings, stringMap, m.virtualRoot));
    w.writeU32(stringID(strings, stringMap, m.diskPath));
  }
  w.endChunk();

  w.beginChunk(static_cast<uint32_t>(NyxProjChunk::STRT), 1);
  w.writeU32(startupID);
  w.endChunk();

  w.beginChunk(static_cast<uint32_t>(NyxProjChunk::EDTR), 1);
  w.writeF32(p.editor.cameraSpeed);
  w.writeU8(p.editor.showGrid ? 1u : 0u);
  w.writeU32(p.editor.gizmoMode);
  w.endChunk();

  w.finalize();
  return true;
}

bool ProjectSerializer::load(const std::string &path, NyxProject &p) {
  NyxBinaryReader r(path);
  if (!r.ok())
    return false;

  uint64_t magic = 0;
  uint32_t version = 0;
  if (!r.readSceneHeader(magic, version))
    return false;
  if (magic != NYXPROJ_MAGIC)
    return false;

  const uint32_t fileMajor = version & 0xFFFF0000u;
  const uint32_t localMajor = NYXPROJ_VERSION & 0xFFFF0000u;
  if (fileMajor != localMajor)
    return false;

  if (!r.loadTOC())
    return false;

  NyxProject loaded{};
  std::vector<std::string> strings;

  if (auto c = r.findChunk(static_cast<uint32_t>(NyxProjChunk::STRS)); c) {
    r.seek(c->offset);
    uint32_t f = 0, v = 0;
    uint64_t s = 0;
    if (!r.readChunkHeader(f, v, s))
      return false;

    const uint32_t count = r.readU32();
    strings.resize(count);
    for (uint32_t i = 0; i < count; ++i)
      strings[i] = readString(r);
  }

  if (auto c = r.findChunk(static_cast<uint32_t>(NyxProjChunk::HEAD)); c) {
    r.seek(c->offset);
    uint32_t f = 0, v = 0;
    uint64_t s = 0;
    if (!r.readChunkHeader(f, v, s))
      return false;

    const uint32_t nameID = r.readU32();
    const uint32_t lastSceneID = r.readU32();
    loaded.name = getStringByID(strings, nameID);
    loaded.lastScene = getStringByID(strings, lastSceneID);
  }

  if (auto c = r.findChunk(static_cast<uint32_t>(NyxProjChunk::MNT)); c) {
    r.seek(c->offset);
    uint32_t f = 0, v = 0;
    uint64_t s = 0;
    if (!r.readChunkHeader(f, v, s))
      return false;

    const uint32_t count = r.readU32();
    loaded.mounts.clear();
    loaded.mounts.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t vr = r.readU32();
      const uint32_t dp = r.readU32();
      AssetMount m{};
      m.virtualRoot = getStringByID(strings, vr);
      m.diskPath = getStringByID(strings, dp);
      loaded.mounts.push_back(std::move(m));
    }
  }

  if (auto c = r.findChunk(static_cast<uint32_t>(NyxProjChunk::STRT)); c) {
    r.seek(c->offset);
    uint32_t f = 0, v = 0;
    uint64_t s = 0;
    if (!r.readChunkHeader(f, v, s))
      return false;
    loaded.startupScene = getStringByID(strings, r.readU32());
  }

  if (auto c = r.findChunk(static_cast<uint32_t>(NyxProjChunk::EDTR)); c) {
    r.seek(c->offset);
    uint32_t f = 0, v = 0;
    uint64_t s = 0;
    if (!r.readChunkHeader(f, v, s))
      return false;
    loaded.editor.cameraSpeed = r.readF32();
    loaded.editor.showGrid = (r.readU8() != 0);
    loaded.editor.gizmoMode = r.readU32();
  }

  p = std::move(loaded);
  return true;
}

} // namespace Nyx
