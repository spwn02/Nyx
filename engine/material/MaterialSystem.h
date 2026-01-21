#pragma once

#include "scene/MaterialData.h"
#include "MaterialHandle.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Nyx {

// GPU packed layout must match shader struct.
struct GpuMaterialPacked {
  glm::vec4 baseColor;
  glm::vec4 mrAoCut; // x=metallic y=roughness z=ao w=alphaCutoff
  glm::vec4 flags;   // x=alphaMasked (0/1) others reserved
};

class MaterialSystem final {
public:
  MaterialHandle create(const MaterialData &d);
  bool isAlive(MaterialHandle h) const;

  MaterialData &get(MaterialHandle h);
  const MaterialData &get(MaterialHandle h) const;

  uint32_t gpuIndex(MaterialHandle h) const; // stable index into SSBO table

  void markDirty(MaterialHandle h);

  MaterialData &edit(MaterialHandle h); // mark dirty and return editable ref

  // GPU
  void initGL();
  void shutdownGL();
  void uploadIfDirty();

  uint32_t ssbo() const { return m_ssbo; } // GL buffer name
  uint32_t gpuCount() const { return (uint32_t)m_gpuTable.size(); }

private:
  struct Slot {
    MaterialData data{};
    uint32_t gen = 1;
    uint32_t gpuIdx = 0;
    bool alive = false;
    bool dirty = false;
  };

  std::vector<Slot> m_slots;
  std::vector<uint32_t> m_free;

  std::vector<GpuMaterialPacked> m_gpuTable;
  bool m_gpuDirty = false;

  uint32_t m_ssbo = 0;

  void ensureSlot(uint32_t s);
  void rebuildGpuTableIfNeeded();
};

} // namespace Nyx
