#pragma once

#include "material/MaterialHandle.h"
#include "scene/material/MaterialTypes.h"

namespace Nyx {

class EngineContext;
class MaterialSystem;

class InspectorMaterial final {
public:
  void draw(EngineContext &engine, MaterialHandle &handle);

private:
  // UI helpers
  bool drawSlot(MaterialSystem &materials, MaterialHandle handle,
                MaterialTexSlot slot);
  bool assignSlotFromPath(MaterialSystem &materials, MaterialHandle handle,
                          MaterialTexSlot slot, const std::string &absPath);
  bool clearSlot(MaterialSystem &materials, MaterialHandle handle,
                 MaterialTexSlot slot);
  bool reloadSlot(MaterialSystem &materials, MaterialHandle handle,
                  MaterialTexSlot slot);

  static bool acceptTexturePathDrop(std::string &outAbsPath);
};

} // namespace Nyx
