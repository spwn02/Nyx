#include "NyxProjIO.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Nyx {

namespace fs = std::filesystem;

static void writeU32(std::vector<uint8_t> &out, uint32_t v) {
  out.push_back((uint8_t)(v & 0xFF));
  out.push_back((uint8_t)((v >> 8) & 0xFF));
  out.push_back((uint8_t)((v >> 16) & 0xFF));
  out.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void writeU16(std::vector<uint8_t> &out, uint16_t v) {
  out.push_back((uint8_t)(v & 0xFF));
  out.push_back((uint8_t)((v >> 8) & 0xFF));
}

static void writeF32(std::vector<uint8_t> &out, float f) {
  static_assert(sizeof(float) == 4);
  uint32_t v = 0;
  std::memcpy(&v, &f, 4);
  writeU32(out, v);
}

static void writeU8(std::vector<uint8_t> &out, uint8_t v) { out.push_back(v); }

static bool readU32(const uint8_t *&p, const uint8_t *end, uint32_t &out) {
  if ((end - p) < 4)
    return false;
  out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
  p += 4;
  return true;
}

static bool readU16(const uint8_t *&p, const uint8_t *end, uint16_t &out) {
  if ((end - p) < 2)
    return false;
  out = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
  p += 2;
  return true;
}

static bool readU8(const uint8_t *&p, const uint8_t *end, uint8_t &out) {
  if ((end - p) < 1)
    return false;
  out = *p++;
  return true;
}

static bool readF32(const uint8_t *&p, const uint8_t *end, float &out) {
  uint32_t v = 0;
  if (!readU32(p, end, v))
    return false;
  std::memcpy(&out, &v, 4);
  return true;
}

// Strings are stored as: u32 byteLen + raw bytes (UTF-8)
static void writeStr(std::vector<uint8_t> &out, const std::string &s) {
  writeU32(out, (uint32_t)s.size());
  out.insert(out.end(), s.begin(), s.end());
}

static bool readStr(const uint8_t *&p, const uint8_t *end, std::string &out) {
  uint32_t n = 0;
  if (!readU32(p, end, n))
    return false;
  if ((uint32_t)(end - p) < n)
    return false;
  out.assign((const char *)p, (size_t)n);
  p += n;
  return true;
}

// -----------------------------------------------------------------------------
// Chunked layout
//
// File: Header + [Chunks...]
//
// Chunk header: u32 tag, u32 sizeBytes, payload[sizeBytes]
//
// Tags:
//  'INFO' -> project name (string)
//  'ROOT' -> assetRootRel (string)
//  'SCNS' -> scenes list
//  'SETT' -> settings
//
// SCNS payload:
//  u32 count
//  repeated:
//    string relPath
//    string name
//
// SETT payload:
//  f32 exposure
//  u8 vsync
//  string startupScene
// -----------------------------------------------------------------------------

static constexpr uint32_t TAG_INFO = 0x4F464E49; // 'INFO'
static constexpr uint32_t TAG_ROOT = 0x544F4F52; // 'ROOT'
static constexpr uint32_t TAG_SCNS = 0x534E4353; // 'SCNS'
static constexpr uint32_t TAG_SETT = 0x54544553; // 'SETT'

static void writeChunk(std::vector<uint8_t> &out, uint32_t tag,
                       const std::vector<uint8_t> &payload) {
  writeU32(out, tag);
  writeU32(out, (uint32_t)payload.size());
  out.insert(out.end(), payload.begin(), payload.end());
}

std::string NyxProjIO::dirname(const std::string &absPath) {
  fs::path p(absPath);
  return p.parent_path().string();
}

std::string NyxProjIO::joinPath(const std::string &a, const std::string &b) {
  fs::path pa(a);
  fs::path pb(b);
  return (pa / pb).lexically_normal().string();
}

bool NyxProjIO::save(const std::string &absPath, const NyxProject &proj) {
  std::vector<uint8_t> file;

  // Header
  writeU32(file, proj.header.magic);
  writeU16(file, proj.header.verMajor);
  writeU16(file, proj.header.verMinor);

  // INFO
  {
    std::vector<uint8_t> p;
    writeStr(p, proj.name);
    writeChunk(file, TAG_INFO, p);
  }

  // ROOT
  {
    std::vector<uint8_t> p;
    writeStr(p, proj.assetRootRel);
    writeChunk(file, TAG_ROOT, p);
  }

  // SCNS
  {
    std::vector<uint8_t> p;
    writeU32(p, (uint32_t)proj.scenes.size());
    for (const auto &s : proj.scenes) {
      writeStr(p, s.relPath);
      writeStr(p, s.name);
    }
    writeChunk(file, TAG_SCNS, p);
  }

  // SETT
  {
    std::vector<uint8_t> p;
    writeF32(p, proj.settings.exposure);
    writeU8(p, proj.settings.vsync ? 1u : 0u);
    writeStr(p, proj.settings.startupScene);
    writeChunk(file, TAG_SETT, p);
  }

  // Write to disk
  std::FILE *f = std::fopen(absPath.c_str(), "wb");
  if (!f)
    return false;
  const size_t wrote = std::fwrite(file.data(), 1, file.size(), f);
  std::fclose(f);
  return wrote == file.size();
}

static bool readAllBytes(const std::string &absPath, std::vector<uint8_t> &out) {
  std::FILE *f = std::fopen(absPath.c_str(), "rb");
  if (!f)
    return false;
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz <= 0) {
    std::fclose(f);
    return false;
  }
  out.resize((size_t)sz);
  const size_t got = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  return got == out.size();
}

static bool parseINFO(const uint8_t *p, const uint8_t *end, NyxProject &proj) {
  return readStr(p, end, proj.name);
}

static bool parseROOT(const uint8_t *p, const uint8_t *end, NyxProject &proj) {
  return readStr(p, end, proj.assetRootRel);
}

static bool parseSCNS(const uint8_t *p, const uint8_t *end, NyxProject &proj) {
  uint32_t count = 0;
  if (!readU32(p, end, count))
    return false;
  proj.scenes.clear();
  proj.scenes.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    NyxProjectSceneEntry e{};
    if (!readStr(p, end, e.relPath))
      return false;
    if (!readStr(p, end, e.name))
      return false;
    proj.scenes.push_back(std::move(e));
  }
  return true;
}

static bool parseSETT(const uint8_t *p, const uint8_t *end, NyxProject &proj) {
  if (!readF32(p, end, proj.settings.exposure))
    return false;
  uint8_t vs = 1;
  if (!readU8(p, end, vs))
    return false;
  proj.settings.vsync = (vs != 0);
  if (!readStr(p, end, proj.settings.startupScene))
    return false;
  return true;
}

std::optional<NyxProjLoadResult> NyxProjIO::load(const std::string &absPath) {
  std::vector<uint8_t> bytes;
  if (!readAllBytes(absPath, bytes))
    return std::nullopt;

  const uint8_t *p = bytes.data();
  const uint8_t *end = bytes.data() + bytes.size();

  NyxProjLoadResult out{};
  out.projectFileAbs = absPath;
  out.projectDirAbs = dirname(absPath);

  uint32_t magic = 0;
  uint16_t maj = 0;
  uint16_t min = 0;
  if (!readU32(p, end, magic))
    return std::nullopt;
  if (!readU16(p, end, maj))
    return std::nullopt;
  if (!readU16(p, end, min))
    return std::nullopt;

  if (magic != NYXPROJ_MAGIC)
    return std::nullopt;
  if (maj != NYXPROJ_VER_MAJOR)
    return std::nullopt; // v1 only

  out.proj.header.magic = magic;
  out.proj.header.verMajor = maj;
  out.proj.header.verMinor = min;

  // Defaults (so missing chunks are okay)
  out.proj.name = "NyxProject";
  out.proj.assetRootRel = "Content";
  out.proj.settings.exposure = 1.0f;
  out.proj.settings.vsync = true;
  out.proj.settings.startupScene.clear();
  out.proj.scenes.clear();

  // chunks
  while (p < end) {
    uint32_t tag = 0;
    uint32_t sz = 0;
    if (!readU32(p, end, tag))
      return std::nullopt;
    if (!readU32(p, end, sz))
      return std::nullopt;
    if ((uint32_t)(end - p) < sz)
      return std::nullopt;

    const uint8_t *c0 = p;
    const uint8_t *c1 = p + sz;

    bool ok = true;
    switch (tag) {
    case TAG_INFO:
      ok = parseINFO(c0, c1, out.proj);
      break;
    case TAG_ROOT:
      ok = parseROOT(c0, c1, out.proj);
      break;
    case TAG_SCNS:
      ok = parseSCNS(c0, c1, out.proj);
      break;
    case TAG_SETT:
      ok = parseSETT(c0, c1, out.proj);
      break;
    default:
      // unknown chunk -> skip for forward compat
      ok = true;
      break;
    }

    if (!ok)
      return std::nullopt;
    p = c1;
  }

  return out;
}

} // namespace Nyx
