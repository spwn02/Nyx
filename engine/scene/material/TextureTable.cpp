#include "render/material/TextureTable.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "render/gl/GLResources.h"
#include <glad/glad.h>

namespace Nyx {

void TextureTable::init(GLResources &gl) {
  m_gl = &gl;
  m_entries.clear();
  m_textures.clear();
}

void TextureTable::shutdown() {
  for (const Entry &entry : m_entries) {
    if (entry.glTex != 0) {
      glDeleteTextures(1, &entry.glTex);
    }
  }
  m_entries.clear();
  m_textures.clear();
  m_gl = nullptr;
}

int TextureTable::find(const std::string &path, bool srgb) const {
  for (size_t i = 0; i < m_entries.size(); ++i) {
    if (m_entries[i].path == path && m_entries[i].srgb == srgb) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

uint32_t TextureTable::getOrCreate2D(const std::string &path, bool srgb) {
  if (!m_gl || path.empty())
    return Invalid;

  if (int idx = find(path, srgb); idx >= 0)
    return static_cast<uint32_t>(idx);

  uint32_t tex = m_gl->createTexture2DFromFile(path, srgb);
  if (!tex) {
    Log::Warn("TextureTable: failed to load '{}'", path);
    return Invalid;
  }

  Entry e{};

  e.path = path;
  e.srgb = srgb;
  e.glTex = tex;

  m_entries.push_back(std::move(e));
  m_textures.push_back(tex);

  return static_cast<uint32_t>(m_entries.size() - 1);
}

void TextureTable::bindAll(uint32_t firstUnit, uint32_t maxCount) const {
  size_t count = m_textures.size();
  if (maxCount > 0 && count > maxCount)
    count = maxCount;
  for (size_t i = 0; i < count; ++i) {
    glBindTextureUnit(firstUnit + static_cast<uint32_t>(i), m_textures[i]);
  }
}

} // namespace Nyx
