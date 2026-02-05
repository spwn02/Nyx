#pragma once
#include "GpuMaterial.h"
#include "TextureTable.h"
#include "material/MaterialHandle.h"
#include "scene/material/MaterialData.h"
#include <cstdint>
#include <vector>

namespace Nyx {

class GLResources;

class MaterialSystem final {
public:
  void initGL(GLResources &gl);
  void shutdownGL();

  MaterialHandle create(const MaterialData &data);
  bool isAlive(MaterialHandle h) const;

  MaterialData &cpu(MaterialHandle h);
  const MaterialData &cpu(MaterialHandle h) const;
  const GpuMaterialPacked &gpu(MaterialHandle h) const;

  uint32_t gpuIndex(MaterialHandle h) const; // index in SSBO array

  void markDirty(MaterialHandle h);
  void uploadIfDirty();
  void processTextureUploads(uint32_t maxPerFrame = 8) {
    m_tex.processUploads(maxPerFrame);
  }
  void reset();

  uint32_t ssbo() const { return m_ssbo; }
  const TextureTable &textures() const { return m_tex; }
  TextureTable &textures() { return m_tex; }

private:
  struct Slot final {
    MaterialData cpu{};
    GpuMaterialPacked gpu{};
    uint32_t gen = 1;
    bool alive = false;
    bool dirty = true;
  };

  GLResources *m_gl = nullptr;
  TextureTable m_tex{};

  std::vector<Slot> m_slots;
  std::vector<uint32_t> m_free;
  uint32_t m_ssbo = 0;
  bool m_anyDirty = false;

  void rebuildGpuForSlot(uint32_t idx);
};

} // namespace Nyx
