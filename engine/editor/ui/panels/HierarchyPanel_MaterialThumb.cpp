#include "HierarchyPanel.h"

#include "app/EngineContext.h"
#include "material/MaterialHandle.h"
#include "scene/material/MaterialData.h"

#include <glad/glad.h>
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace Nyx {

static uint64_t hashMaterialData(const MaterialData &m) {
  auto hmix = [](uint64_t h, uint64_t v) -> uint64_t {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
  };
  auto hf = [](float v) -> uint64_t {
    uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return (uint64_t)u;
  };
  uint64_t h = 1469598103934665603ull;
  h = hmix(h, hf(m.baseColorFactor.x));
  h = hmix(h, hf(m.baseColorFactor.y));
  h = hmix(h, hf(m.baseColorFactor.z));
  h = hmix(h, hf(m.baseColorFactor.w));
  h = hmix(h, hf(m.emissiveFactor.x));
  h = hmix(h, hf(m.emissiveFactor.y));
  h = hmix(h, hf(m.emissiveFactor.z));
  h = hmix(h, hf(m.metallic));
  h = hmix(h, hf(m.roughness));
  h = hmix(h, hf(m.ao));
  h = hmix(h, hf(m.uvScale.x));
  h = hmix(h, hf(m.uvScale.y));
  h = hmix(h, hf(m.uvOffset.x));
  h = hmix(h, hf(m.uvOffset.y));
  h = hmix(h, (uint64_t)m.alphaMode);
  h = hmix(h, hf(m.alphaCutoff));
  h = hmix(h, m.tangentSpaceNormal ? 0xA5A5A5A5u : 0x5A5A5A5Au);
  for (const auto &p : m.texPath) {
    for (char c : p)
      h = hmix(h, (uint64_t)(uint8_t)c);
  }
  for (char c : m.name)
    h = hmix(h, (uint64_t)(uint8_t)c);
  return h;
}

static std::filesystem::path matPreviewCacheDir() {
  static bool s_init = false;
  static std::filesystem::path s_dir;
  if (!s_init) {
    s_init = true;
    s_dir = std::filesystem::current_path() / ".cache" / "matpreviewcache";
    std::error_code ec;
    std::filesystem::create_directories(s_dir, ec);
  }
  return s_dir;
}

static std::string matPreviewCachePath(MaterialHandle h,
                                       const MaterialData &md,
                                       uint64_t settingsHash) {
  const uint64_t key = (uint64_t(h.slot) << 32) | uint64_t(h.gen);
  const uint64_t dataHash = hashMaterialData(md);
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%016llx_%016llx_%016llx.png",
                (unsigned long long)key, (unsigned long long)dataHash,
                (unsigned long long)settingsHash);
  return (matPreviewCacheDir() / buf).string();
}

HierarchyPanel::MatThumb &
HierarchyPanel::getMaterialThumb(EngineContext &engine, MaterialHandle h) {
  const uint64_t key = (uint64_t(h.slot) << 32) | uint64_t(h.gen);
  const uint32_t lastCaptured = engine.lastPreviewCaptureTex();
  MatThumb &th = m_matThumbs[key];
  if (th.tex == 0) {
    glCreateTextures(GL_TEXTURE_2D, 1, &th.tex);
    glTextureParameteri(th.tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(th.tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(th.tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(th.tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureStorage2D(th.tex, 1, GL_RGBA8, 64, 64);
    const uint32_t zero = 0;
    glClearTexImage(th.tex, 0, GL_RGBA, GL_UNSIGNED_BYTE, &zero);
    th.ready = false;
    th.pending = false;
    th.saved = false;
  }

  if (engine.materials().isAlive(h)) {
    const MaterialData &md = engine.materials().cpu(h);
    th.cachePath = matPreviewCachePath(h, md, m_matThumbSettingsHash);
  } else {
    th.cachePath.clear();
  }

  if (!th.ready && !th.pending && !th.cachePath.empty()) {
    if (std::filesystem::exists(th.cachePath)) {
      int w = 0;
      int hgt = 0;
      int comp = 0;
      unsigned char *data = stbi_load(th.cachePath.c_str(), &w, &hgt, &comp, 4);
      if (data && w > 0 && hgt > 0) {
        glTextureSubImage2D(th.tex, 0, 0, 0, w, hgt, GL_RGBA, GL_UNSIGNED_BYTE,
                            data);
        stbi_image_free(data);
        th.ready = true;
        th.saved = true;
      } else if (data) {
        stbi_image_free(data);
      }
    }
  }

  if (lastCaptured != 0 && lastCaptured == th.tex) {
    th.ready = true;
    th.pending = false;
  }

  if (!th.ready && !th.pending) {
    engine.requestMaterialPreview(h, th.tex);
    th.pending = true;
  }

  if (th.ready && !th.saved && !th.cachePath.empty()) {
    const int w = 64;
    const int hgt = 64;
    std::vector<uint8_t> rgba((size_t)w * (size_t)hgt * 4u);
    glGetTextureImage(th.tex, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                      (GLsizei)rgba.size(), rgba.data());
    stbi_write_png(th.cachePath.c_str(), w, hgt, 4, rgba.data(), w * 4);
    th.saved = true;
  }

  return th;
}

} // namespace Nyx
