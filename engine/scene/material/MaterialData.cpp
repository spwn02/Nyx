#include "scene/material/MaterialData.h"

#include <sstream>

namespace Nyx {

MaterialValidation validateMaterial(const MaterialData &m) {
  MaterialValidation v{};
  std::ostringstream ss;

  if (m.alphaMode == MatAlphaMode::Mask) {
    if (!(m.alphaCutoff > 0.0f && m.alphaCutoff < 1.0f)) {
      v.ok = false;
      ss << "AlphaMode=Mask requires alphaCutoff in (0,1). ";
    }
  }

  if (m.alphaMode == MatAlphaMode::Blend) {
    v.warn = true;
    ss << "AlphaMode=Blend: rendered in transparent pass (no ID write). ";
  }

  if (!m.tangentSpaceNormal) {
    const bool hasNormal =
        !m.texPath[static_cast<size_t>(MaterialTexSlot::Normal)].empty();
    if (hasNormal) {
      v.warn = true;
      ss << "Normal texture is set, but tangentSpaceNormal is disabled. ";
    }
  }

  v.message = ss.str();
  if (!v.ok && v.message.empty())
    v.message = "Material validation failed.";
  return v;
}

} // namespace Nyx
