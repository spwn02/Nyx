#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

class GLResources;

// Owns GL textures for material slots and provides indices for GPU table.
class TextureTable final {
public:
  void init(GLResources &gl);
  void shutdown();

  // Returns texture index in table, or kInvalidTexIndex if load failed.
  uint32_t getOrCreate2D(const std::string &path, bool srgb);

  const std::vector<uint32_t> &glTextures() const { return m_textures; }

  void bindAll(uint32_t firstUnit = 0, uint32_t maxCount = 0) const;

  static constexpr uint32_t Invalid = 0xFFFFFFFF;

private:
  GLResources *m_gl = nullptr;

  struct Entry final {
    std::string path;
    bool srgb = false;
    uint32_t glTex = 0;
  };

  std::vector<Entry> m_entries;
  std::vector<uint32_t> m_textures; // parallel glTex list for fast bind

  int find(const std::string &path, bool srgb) const;
};

} // namespace Nyx
