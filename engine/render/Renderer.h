#pragma once

#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLMesh.h"
#include "render/gl/GLResources.h"
#include "render/passes/PassDepthPre.h"
#include "render/passes/PassForwardMRT.h"
#include "render/passes/PassPostFilters.h"
#include "render/passes/PassPresent.h"
#include "render/passes/PassSelection.h"
#include "render/passes/PassTonemapRG.h"
#include "render/rg/RGResource.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderPassContext.h"
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

  uint32_t renderFrame(const RenderPassContext &ctx, bool editorVisible,
                       const RenderableRegistry &registry,
                       const std::vector<uint32_t> &selected,
                       EngineContext &engine);

  uint32_t readPickID(uint32_t px, uint32_t py, uint32_t fbHeight) const;

  void setSelectedPickIDs(const std::vector<uint32_t> &ids);
  void drawPrimitive(ProcMeshType type);

private:
  void ensureTargets(uint32_t w, uint32_t h);
  void ensureScene();

  void ensurePrimitiveMeshes();

private:
  RenderGraph m_graph;
  RGResources m_rgRes;
  FrameOutputs m_out;
  GLResources m_res;

  GLFullscreenTriangle m_fsTri;
  uint32_t m_hdrFbo, m_outlineFbo;
  uint32_t m_presentProg, m_forwardProg, m_outlineProg;
  uint32_t m_selectedSSBO, m_selectedCount;

  PassDepthPre m_passDepthPre;
  PassForwardMRT m_passForward;
  PassTonemapRG m_passTonemap;
  PassPostFilters m_passPost;
  PassSelection m_passSelection;
  PassPresent m_passPresent;

  GLMesh m_primMeshes[5]{};
  bool m_primReady[5]{false, false, false, false, false};
};

} // namespace Nyx
