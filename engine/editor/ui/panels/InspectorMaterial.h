#pragma once

#include <cstdint>

namespace Nyx {

class MaterialSystem;
struct MaterialHandle;

class InspectorMaterial final {
public:
  // Draw inspector for a material. If material is invalid/not alive - draws
  // nothing.
  void draw(MaterialSystem &materialSystem, MaterialHandle handle);

private:
  void drawTextureSlot(MaterialSystem &materialSystem, MaterialHandle handle,
                       const char *label, uint32_t slotIndex, bool srgb);
};

} // namespace Nyx
