#include "DragDropAsset.h"

#include "project/NyxProjectRuntime.h"
#include "render/material/TextureTable.h"

#include <imgui.h>

#include <cctype>
#include <cstring>
#include <string>

namespace Nyx {

static bool isTexturePath(const std::string &rel) {
  std::string lower = rel;
  for (char &c : lower)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  auto ends = [&](const char *e) {
    const size_t n = std::strlen(e);
    if (lower.size() < n)
      return false;
    return lower.compare(lower.size() - n, n, e) == 0;
  };
  return ends(".png") || ends(".jpg") || ends(".jpeg") || ends(".tga") ||
         ends(".bmp") || ends(".ktx") || ends(".ktx2") || ends(".hdr") ||
         ends(".exr");
}

// Example hook: drop onto slot to load texture.
static void materialSlotDropTarget(TextureTable &table, NyxProjectRuntime &proj,
                                   bool srgb,
                                   uint32_t &ioTexIndex /*TextureTable index*/,
                                   bool *outChanged) {
  if (outChanged)
    *outChanged = false;

  if (!ImGui::BeginDragDropTarget())
    return;

  std::string rel;
  if (DragDropAsset::acceptRelPath(rel)) {
    if (isTexturePath(rel)) {
      const std::string abs = proj.makeAbsolute(rel);
      const uint32_t idx = table.getOrCreate2D(abs, srgb);
      if (idx != TextureTable::Invalid) {
        ioTexIndex = idx;
        if (outChanged)
          *outChanged = true;
      }
    }
  }

  ImGui::EndDragDropTarget();
}

} // namespace Nyx
