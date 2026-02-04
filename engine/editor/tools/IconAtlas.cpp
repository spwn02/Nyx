#include "IconAtlas.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <nlohmann/json.hpp>

namespace Nyx {

IconAtlas::~IconAtlas() {
  if (m_glTex) {
    glDeleteTextures(1, &m_glTex);
    m_glTex = 0;
  }
}

static std::string readTextFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return {};
  std::string s((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
  return s;
}

static std::string dirnameOf(const std::string &path) {
  auto p = path.find_last_of("/\\");
  if (p == std::string::npos)
    return {};
  return path.substr(0, p + 1);
}

static std::string toLower(std::string v) {
  for (char &c : v)
    c = (char)std::tolower((unsigned char)c);
  return v;
}

bool IconAtlas::loadTextureRGBA8(const std::string &pngPath) {
  int w = 0, h = 0, comp = 0;
  stbi_uc *pixels = stbi_load(pngPath.c_str(), &w, &h, &comp, 4);
  if (!pixels)
    return false;

  m_w = w;
  m_h = h;

  if (!m_glTex)
    glGenTextures(1, &m_glTex);
  glBindTexture(GL_TEXTURE_2D, m_glTex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);

  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(pixels);
  return true;
}

bool IconAtlas::loadFromJson(const std::string &jsonPath) {
  m_regions.clear();

  const std::string text = readTextFile(jsonPath);
  if (text.empty())
    return false;

  nlohmann::json j =
      nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded())
    return false;

  const std::string baseDir = dirnameOf(jsonPath);

  const std::string imageName = j.value("image", "");
  const int atlasW = j.value("w", 0);
  const int atlasH = j.value("h", 0);
  if (imageName.empty() || atlasW <= 0 || atlasH <= 0)
    return false;

  if (!loadTextureRGBA8(baseDir + imageName))
    return false;

  // Optional sanity: if JSON says 512x512 but png differs, prefer png
  const float invW = 1.0f / float(m_w);
  const float invH = 1.0f / float(m_h);

  const auto &sprites = j["sprites"];
  if (!sprites.is_object())
    return false;

  for (auto it = sprites.begin(); it != sprites.end(); ++it) {
    const std::string name = it.key();
    const auto &r = it.value();

    int x = r.value("x", 0);
    int y = r.value("y", 0);
    int w = r.value("w", 0);
    int h = r.value("h", 0);
    if (w <= 0 || h <= 0)
      continue;

    AtlasRegion reg{};
    reg.pxSize = ImVec2((float)w, (float)h);
    reg.uv0 = ImVec2(x * invW, y * invH);
    reg.uv1 = ImVec2((x + w) * invW, (y + h) * invH);
    m_regions.emplace(name, reg);
  }

  return !m_regions.empty();
}

bool IconAtlas::buildFromFolder(const std::string &folderPath,
                                const std::string &outJsonPath,
                                const std::string &outPngPath, int iconSize,
                                int padding) {
  m_regions.clear();

  if (iconSize <= 0)
    return false;

  std::filesystem::path dir = folderPath;
  if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir))
    return false;

  struct IconEntry {
    std::string name;
    std::filesystem::path path;
    std::vector<uint8_t> rgba; // iconSize * iconSize * 4
  };

  std::vector<IconEntry> icons;

  for (const auto &entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;
    const std::filesystem::path p = entry.path();
    const std::string ext = toLower(p.extension().string());
    if (ext != ".png")
      continue;
    if (!outPngPath.empty()) {
      const std::filesystem::path outPng = outPngPath;
      if (std::filesystem::exists(outPng) &&
          std::filesystem::equivalent(p, outPng))
        continue;
    }
    IconEntry e{};
    e.name = p.stem().string();
    e.path = p;
    icons.push_back(std::move(e));
  }

  if (icons.empty())
    return false;

  std::sort(icons.begin(), icons.end(),
            [](const IconEntry &a, const IconEntry &b) {
              return a.name < b.name;
            });

  for (auto &icon : icons) {
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels = stbi_load(icon.path.string().c_str(), &w, &h, &comp, 4);
    if (!pixels)
      continue;

    icon.rgba.resize((size_t)iconSize * (size_t)iconSize * 4u);

    if (w != iconSize || h != iconSize) {
      unsigned char *resized = stbir_resize_uint8_linear(
          pixels, w, h, 0, icon.rgba.data(), iconSize, iconSize, 0,
          STBIR_RGBA);
      if (!resized) {
        stbi_image_free(pixels);
        icon.rgba.clear();
        continue;
      }
    } else {
      std::memcpy(icon.rgba.data(), pixels,
                  (size_t)iconSize * (size_t)iconSize * 4u);
    }

    stbi_image_free(pixels);

    bool anyRgb = false;
    bool anyAlpha = false;
    uint8_t maxAlpha = 0;
    for (size_t i = 0; i < icon.rgba.size(); i += 4) {
      const uint8_t a = icon.rgba[i + 3];
      if (a)
        anyAlpha = true;
      if (a > maxAlpha)
        maxAlpha = a;
      if (icon.rgba[i] || icon.rgba[i + 1] || icon.rgba[i + 2])
        anyRgb = true;
      if (anyRgb && anyAlpha)
        break;
    }

    if (!anyRgb && anyAlpha) {
      for (size_t i = 0; i < icon.rgba.size(); i += 4) {
        const uint8_t a = icon.rgba[i + 3];
        if (!a)
          continue;
        icon.rgba[i] = 255;
        icon.rgba[i + 1] = 255;
        icon.rgba[i + 2] = 255;
      }
    }

    if (maxAlpha > 0 && maxAlpha < 255) {
      for (size_t i = 0; i < icon.rgba.size(); i += 4) {
        const uint8_t a = icon.rgba[i + 3];
        if (!a)
          continue;
        const int scaled = (int)a * 255 / (int)maxAlpha;
        icon.rgba[i + 3] = (uint8_t)std::clamp(scaled, 0, 255);
      }
    }
  }

  icons.erase(std::remove_if(icons.begin(), icons.end(),
                             [](const IconEntry &e) { return e.rgba.empty(); }),
              icons.end());

  if (icons.empty())
    return false;

  const int count = (int)icons.size();
  const int cols = (int)std::ceil(std::sqrt((float)count));
  const int rows = (count + cols - 1) / cols;

  const int atlasW = cols * iconSize + (cols - 1) * padding;
  const int atlasH = rows * iconSize + (rows - 1) * padding;

  std::vector<uint8_t> atlas;
  atlas.resize((size_t)atlasW * (size_t)atlasH * 4u, 0u);

  nlohmann::json j;
  j["image"] = std::filesystem::path(outPngPath).filename().string();
  j["w"] = atlasW;
  j["h"] = atlasH;
  nlohmann::json sprites = nlohmann::json::object();

  for (int i = 0; i < count; ++i) {
    const int col = i % cols;
    const int row = i / cols;
    const int x = col * (iconSize + padding);
    const int y = row * (iconSize + padding);

    const auto &icon = icons[i];

    for (int iy = 0; iy < iconSize; ++iy) {
      uint8_t *dst =
          atlas.data() + ((y + iy) * atlasW + x) * 4u;
      const uint8_t *src =
          icon.rgba.data() + (size_t)iy * (size_t)iconSize * 4u;
      std::memcpy(dst, src, (size_t)iconSize * 4u);
    }

    nlohmann::json r;
    r["x"] = x;
    r["y"] = y;
    r["w"] = iconSize;
    r["h"] = iconSize;
    sprites[icon.name] = r;
  }

  j["sprites"] = std::move(sprites);

  if (!outPngPath.empty()) {
    std::filesystem::path outPng = outPngPath;
    if (outPng.has_parent_path())
      std::filesystem::create_directories(outPng.parent_path());
    const int ok =
        stbi_write_png(outPngPath.c_str(), atlasW, atlasH, 4, atlas.data(),
                       atlasW * 4);
    if (!ok)
      return false;
  }

  if (!outJsonPath.empty()) {
    std::filesystem::path outJson = outJsonPath;
    if (outJson.has_parent_path())
      std::filesystem::create_directories(outJson.parent_path());
    std::ofstream f(outJsonPath, std::ios::binary);
    if (!f)
      return false;
    const std::string txt = j.dump(2);
    f.write(txt.data(), (std::streamsize)txt.size());
  }

  if (!outJsonPath.empty())
    return loadFromJson(outJsonPath);

  return true;
}

const AtlasRegion *IconAtlas::find(const std::string &name) const {
  auto it = m_regions.find(name);
  if (it == m_regions.end())
    return nullptr;
  return &it->second;
}

const AtlasRegion &IconAtlas::getOr(const std::string &name,
                                    const AtlasRegion &fallback) const {
  if (auto *r = find(name))
    return *r;
  return fallback;
}

} // namespace Nyx
