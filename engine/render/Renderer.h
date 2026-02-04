#pragma once

#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLMesh.h"
#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/PassDepthPre.h"
#include "render/passes/PassEnvBRDFLUT.h"
#include "render/passes/PassEnvEquirectToCube.h"
#include "render/passes/PassEnvIrradiance.h"
#include "render/passes/PassEnvPrefilter.h"
#include "render/passes/PassForwardMRT.h"
#include "render/passes/PassHiZBuild.h"
#include "render/passes/PassShadowCSM.h"
#include "render/passes/PassShadowDebugOverlay.h"
#include "render/passes/PassShadowSpot.h"
#include "render/passes/PassShadowDir.h"
#include "render/passes/PassShadowPoint.h"
#include "render/passes/PassLightCluster.h"
#include "render/passes/PassLightGridDebug.h"
#include "render/passes/PassPostFilters.h"
#include "render/passes/PassPresent.h"
#include "render/passes/PassSelection.h"
#include "render/passes/PassSkyIBL.h"
#include "render/passes/PassTonemap.h"
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

  GLShaderUtil &shaders() { return m_shaders; }
  const GLShaderUtil &shaders() const { return m_shaders; }

  GLResources &resources() { return m_res; }
  const GLResources &resources() const { return m_res; }

  ShadowCSMConfig &shadowCSMConfig() { return m_passShadowCSM.config(); }
  const ShadowCSMConfig &shadowCSMConfig() const {
    return m_passShadowCSM.config();
  }

  const PassShadowSpot& shadowSpotPass() const { return m_passShadowSpot; }
  const PassShadowDir& shadowDirPass() const { return m_passShadowDir; }
  const PassShadowPoint& shadowPointPass() const { return m_passShadowPoint; }

private:
  // void ensureTargets(uint32_t w, uint32_t h);
  // void ensureScene();

  // void ensurePrimitiveMeshes();

private:
  RenderGraph m_graph;
  RGResources m_rgRes;
  FrameOutputs m_out;
  GLResources m_res;
  GLShaderUtil m_shaders{};

  GLFullscreenTriangle m_fsTri;

  PassEnvEquirectToCube m_passEnvEquirect;
  PassEnvIrradiance m_passEnvIrradiance;
  PassEnvPrefilter m_passEnvPrefilter;
  PassEnvBRDFLUT m_passEnvBRDF;

  PassDepthPre m_passDepthPre;
  PassHiZBuild m_passHiZ;
  PassLightCluster m_passLightCluster;
  PassLightGridDebug m_passLightGridDebug;
  PassShadowCSM m_passShadowCSM;
  PassShadowSpot m_passShadowSpot;
  PassShadowDir m_passShadowDir;
  PassShadowPoint m_passShadowPoint;
  PassForwardMRT m_passForward;
  PassSkyIBL m_passSky;
  PassShadowDebugOverlay m_passShadowDebug;
  PassTonemap m_passTonemap;
  PassPostFilters m_passPost;
  PassSelection m_passSelection;
  PassPresent m_passPresent;

  GLMesh m_primMeshes[5]{};
  bool m_primReady[5]{false, false, false, false, false};
};

} // namespace Nyx
