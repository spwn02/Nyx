#pragma once
#include "GpuMaterial.h"
#include "MaterialGraph.h"
#include "MaterialGraphCompiler.h"
#include "TextureTable.h"
#include "material/MaterialHandle.h"
#include "scene/material/MaterialData.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

class GLResources;

class MaterialSystem final {
public:
  struct MaterialSnapshot final {
    uint32_t gen = 1;
    bool alive = false;
    MaterialData cpu{};
    MaterialGraph graph{};
  };
  struct MaterialSystemSnapshot final {
    std::vector<MaterialSnapshot> slots;
    std::vector<uint32_t> free;
    uint64_t changeSerial = 0;
  };

  void initGL(GLResources &gl);
  void shutdownGL();

  MaterialHandle create(const MaterialData &data);
  void destroy(MaterialHandle h);
  bool isAlive(MaterialHandle h) const;

  MaterialData &cpu(MaterialHandle h);
  const MaterialData &cpu(MaterialHandle h) const;
  const GpuMaterialPacked &gpu(MaterialHandle h) const;
  MaterialGraph &graph(MaterialHandle h);
  const MaterialGraph &graph(MaterialHandle h) const;
  const std::string &graphError(MaterialHandle h) const;
  MatAlphaMode alphaMode(MaterialHandle h) const;

  uint32_t gpuIndex(MaterialHandle h) const; // index in SSBO array
  uint32_t slotCount() const { return (uint32_t)m_slots.size(); }
  MaterialHandle handleBySlot(uint32_t slot) const;
  void ensureGraphFromMaterial(MaterialHandle h, bool force = false);
  void syncGraphFromMaterial(MaterialHandle h, bool force = false);
  void syncMaterialFromGraph(MaterialHandle h);

  void markDirty(MaterialHandle h);
  void markGraphDirty(MaterialHandle h);
  void uploadIfDirty();
  uint64_t changeSerial() const { return m_changeSerial; }
  void processTextureUploads(uint32_t maxPerFrame = 8) {
    m_tex.processUploads(maxPerFrame);
  }
  void reset();
  void snapshot(MaterialSystemSnapshot &out) const;
  void restore(const MaterialSystemSnapshot &snap);

  uint32_t ssbo() const { return m_ssbo; }
  uint32_t graphHeadersSSBO() const { return m_graphHeadersSSBO; }
  uint32_t graphNodesSSBO() const { return m_graphNodesSSBO; }
  const TextureTable &textures() const { return m_tex; }
  TextureTable &textures() { return m_tex; }

private:
  struct Slot final {
    MaterialData cpu{};
    GpuMaterialPacked gpu{};
    MaterialGraph graph{};
    CompiledMaterialGraph compiled{};
    std::string graphErr{};
    uint32_t gen = 1;
    bool alive = false;
    bool dirty = true;
    bool graphDirty = true;
  };

  GLResources *m_gl = nullptr;
  TextureTable m_tex{};

  std::vector<Slot> m_slots;
  std::vector<uint32_t> m_free;
  uint32_t m_ssbo = 0;
  uint32_t m_graphHeadersSSBO = 0;
  uint32_t m_graphNodesSSBO = 0;
  bool m_anyGraphDirty = true;
  bool m_anyDirty = false;
  uint64_t m_changeSerial = 1;

  void rebuildGpuForSlot(uint32_t idx);
  void updateGraphTablesIfDirty();
};

} // namespace Nyx
