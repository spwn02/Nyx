#pragma once

#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLMesh.h"
#include "render/gl/GLResources.h"
#include "render/passes/PassTonemap.h"
#include "render/rg/RGResource.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderContext.h"
#include "rg/RenderGraph.h"
#include "scene/RenderableRegistry.h"

namespace Nyx {

class EngineContext;

struct FrameOutputs {
  RGHandle hdr = InvalidRG;
  RGHandle ldr = InvalidRG;
  RGHandle id = InvalidRG;
  RGHandle outlined = InvalidRG;
  RGHandle depth = InvalidRG;
};

class Renderer final {
public:
  Renderer();
  ~Renderer();

  uint32_t renderFrame(const RenderContext &ctx, bool editorVisible,
                       const RenderableRegistry &registry,
                       const std::vector<uint32_t> &selected,
                       EngineContext &engine);

  uint32_t readPickID(uint32_t px, uint32_t py, uint32_t fbHeight) const;

  void setSelectedPickIDs(const std::vector<uint32_t> &ids);

private:
  void ensureTargets(uint32_t w, uint32_t h);
  void ensureScene();

  void ensurePrimitiveMeshes();
  void drawPrimitive(ProcMeshType type);

private:
  RenderGraph m_graph;
  RGResources m_rgRes;
  FrameOutputs m_out;
  GLResources m_res;

  GLFullscreenTriangle m_fsTri;
  PassTonemap m_tonemap;
  uint32_t m_hdrFbo, m_outlineFbo;
  uint32_t m_presentProg, m_forwardProg, m_outlineProg;
  uint32_t m_selectedSSBO, m_selectedCount;

  GLMesh m_primMeshes[5]{};
  bool m_primReady[5]{false, false, false, false, false};
};

} // namespace Nyx
