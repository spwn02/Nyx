#pragma once

#include <cstdint>
#include <imgui.h>
#include <string>
#include <unordered_map>

namespace Nyx {

struct AtlasRegion {
  ImVec2 uv0;    // top-left
  ImVec2 uv1;    // bottom-right
  ImVec2 pxSize; // sprite size in pixels
};

class IconAtlas {
public:
  ~IconAtlas();

  // baseDir: folder that contains json + png, e.g. "app/assets/ui/icons/"
  bool loadFromJson(const std::string &jsonPath);
  // Build atlas (png + json) from a folder of icons and load it.
  // Icon filenames (without extension) become sprite names.
  bool buildFromFolder(const std::string &folderPath,
                       const std::string &outJsonPath,
                       const std::string &outPngPath, int iconSize = 16,
                       int padding = 0);

  ImTextureID imguiTexId() const { return (ImTextureID)(intptr_t)m_glTex; }

  const AtlasRegion *find(const std::string &name) const;
  const AtlasRegion &getOr(const std::string &name,
                           const AtlasRegion &fallback) const;

  int width() const { return m_w; }
  int height() const { return m_h; }

private:
  bool loadTextureRGBA8(const std::string &pngPath);

  unsigned int m_glTex = 0; // GLuint
  int m_w = 0;
  int m_h = 0;

  std::unordered_map<std::string, AtlasRegion> m_regions;
};

} // namespace Nyx
