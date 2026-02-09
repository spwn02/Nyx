#include "EditorUserConfig.h"
#include <cstdio>
#include <filesystem>
#include <vector>

namespace Nyx {

static constexpr uint32_t NYXU_MAGIC = 0x5558594E; // 'NYXU'
static constexpr uint16_t NYXU_VER_MAJOR = 1;
static constexpr uint16_t NYXU_VER_MINOR = 0;

static void wU32(std::vector<uint8_t> &o, uint32_t v) {
  o.push_back((uint8_t)(v & 0xFF));
  o.push_back((uint8_t)((v >> 8) & 0xFF));
  o.push_back((uint8_t)((v >> 16) & 0xFF));
  o.push_back((uint8_t)((v >> 24) & 0xFF));
}

static void wU16(std::vector<uint8_t> &o, uint16_t v) {
  o.push_back((uint8_t)(v & 0xFF));
  o.push_back((uint8_t)((v >> 8) & 0xFF));
}

static bool rU32(const uint8_t *&p, const uint8_t *e, uint32_t &out) {
  if ((e - p) < 4)
    return false;
  out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
        ((uint32_t)p[3] << 24);
  p += 4;
  return true;
}

static bool rU16(const uint8_t *&p, const uint8_t *e, uint16_t &out) {
  if ((e - p) < 2)
    return false;
  out = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
  p += 2;
  return true;
}

static void wStr(std::vector<uint8_t> &o, const std::string &s) {
  wU32(o, (uint32_t)s.size());
  o.insert(o.end(), s.begin(), s.end());
}

static bool rStr(const uint8_t *&p, const uint8_t *e, std::string &out) {
  uint32_t n = 0;
  if (!rU32(p, e, n))
    return false;
  if ((uint32_t)(e - p) < n)
    return false;
  out.assign((const char *)p, (size_t)n);
  p += n;
  return true;
}

static bool readAll(const std::string &abs, std::vector<uint8_t> &out) {
  std::FILE *f = std::fopen(abs.c_str(), "rb");
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

bool EditorUserConfigIO::save(const std::string &absPath,
                              const EditorUserConfig &cfg) {
  std::error_code ec;
  const std::filesystem::path p(absPath);
  if (p.has_parent_path())
    std::filesystem::create_directories(p.parent_path(), ec);

  std::vector<uint8_t> b;
  wU32(b, NYXU_MAGIC);
  wU16(b, NYXU_VER_MAJOR);
  wU16(b, NYXU_VER_MINOR);

  // recent list
  wU32(b, (uint32_t)cfg.recent.items.size());
  for (const auto &p : cfg.recent.items)
    wStr(b, p);

  std::FILE *f = std::fopen(absPath.c_str(), "wb");
  if (!f)
    return false;
  const size_t wrote = std::fwrite(b.data(), 1, b.size(), f);
  std::fclose(f);
  return wrote == b.size();
}

std::optional<EditorUserConfig> EditorUserConfigIO::load(
    const std::string &absPath) {
  std::vector<uint8_t> b;
  if (!readAll(absPath, b))
    return std::nullopt;

  const uint8_t *p = b.data();
  const uint8_t *e = b.data() + b.size();

  uint32_t magic = 0;
  uint16_t maj = 0, min = 0;
  if (!rU32(p, e, magic))
    return std::nullopt;
  if (!rU16(p, e, maj))
    return std::nullopt;
  if (!rU16(p, e, min))
    return std::nullopt;

  if (magic != NYXU_MAGIC)
    return std::nullopt;
  if (maj != NYXU_VER_MAJOR)
    return std::nullopt;

  EditorUserConfig cfg{};

  uint32_t count = 0;
  if (!rU32(p, e, count))
    return std::nullopt;
  cfg.recent.items.clear();
  cfg.recent.items.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::string s;
    if (!rStr(p, e, s))
      return std::nullopt;
    cfg.recent.items.push_back(std::move(s));
  }

  return cfg;
}

} // namespace Nyx
