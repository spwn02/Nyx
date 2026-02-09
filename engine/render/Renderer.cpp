#include "Renderer.h"

#include "app/EngineContext.h"
#include "npgms/MeshCPU.h"
#include "npgms/PrimitiveGenerator.h"
#include "render/gl/GLShaderUtil.h"
#include "scene/RenderableRegistry.h"

#include <algorithm>
#include <filesystem>
#include <glad/glad.h>
#include <vector>

namespace Nyx {

Renderer::Renderer() : m_rgRes(m_res) {
  m_fsTri.init();
  m_shaders.setShaderRoot("engine/resources/shaders");

  const char *rgDump = std::getenv("NYX_RG_DUMP");
  if (rgDump && rgDump[0] != '\0' && rgDump[0] != '0') {
    std::filesystem::path path =
        std::filesystem::current_path() / ".cache" / "rendergraph.dot";
    std::filesystem::create_directories(path.parent_path());
    m_graph.enableDebug(path.string(), /*dumpLifetimes=*/true);
  }

  m_passEnvEquirect.configure(m_shaders);
  m_passEnvIrradiance.configure(m_shaders);
  m_passEnvPrefilter.configure(m_shaders);
  m_passEnvBRDF.configure(m_shaders);

  m_passDepthPre.configure(m_shaders, m_res,
                           [this](ProcMeshType t) { drawPrimitive(t); });
  m_passShadowCSM.configure(m_shaders, m_res,
                            [this](ProcMeshType t) { drawPrimitive(t); });
  m_passShadowSpot.configure(m_shaders, m_res,
                             [this](ProcMeshType t) { drawPrimitive(t); });
  m_passShadowDir.configure(m_shaders, m_res,
                            [this](ProcMeshType t) { drawPrimitive(t); });
  m_passShadowPoint.configure(m_shaders, m_res,
                              [this](ProcMeshType t) { drawPrimitive(t); });
  m_passHiZ.configure(m_shaders);
  m_passLightCluster.configure(m_shaders);
  m_passLightGridDebug.configure(m_shaders);
  m_passForwardOpaque.configure(m_shaders, m_res,
                                [this](ProcMeshType t) { drawPrimitive(t); });
  m_passForwardTransparent.configure(
      m_shaders, m_res, [this](ProcMeshType t) { drawPrimitive(t); });
  m_passPickID.configure(m_shaders, m_res);
  m_passTransparentOIT.configure(m_shaders, m_res);
  m_passTransparentOITComposite.configure(m_shaders);
  m_passPreview.configure(m_shaders, m_res,
                          [this](ProcMeshType t) { drawPrimitive(t); });
  m_passSky.configure(m_shaders);
  m_passShadowDebug.configure(m_shaders);
  m_passTonemap.configure(m_shaders);
  m_passPost.configure(m_shaders);
  m_passSelection.configure(m_shaders, m_res, m_fsTri);
  m_passSelectionMaskTransparent.configure(
      m_shaders, m_res, [this](ProcMeshType t) { drawPrimitive(t); });
  m_passPresent.configure(m_shaders, m_fsTri);
}

Renderer::~Renderer() { m_fsTri.shutdown(); }

void Renderer::setSelectedPickIDs(const std::vector<uint32_t> &ids,
                                  uint32_t activePick) {
  m_passSelection.updateSelectedIDs(ids, activePick);
  m_passSelectionMaskTransparent.updateSelectedIDs(ids);
}

static uint32_t primIndex(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return 0;
  case ProcMeshType::Plane:
    return 1;
  case ProcMeshType::Circle:
    return 2;
  case ProcMeshType::Sphere:
    return 3;
  case ProcMeshType::Monkey:
    return 4;
  default:
    return 0;
  }
}

void Renderer::drawPrimitive(ProcMeshType t) {
  const uint32_t i = primIndex(t);
  if (!m_primReady[i]) {
    MeshCPU cpu = makePrimitivePN(t, 32);
    m_primMeshes[i].upload(cpu);
    m_primReady[i] = true;
  }
  m_primMeshes[i].draw();
}

void Renderer::drawPrimitiveBaseInstance(ProcMeshType t,
                                         uint32_t baseInstance) {
  const uint32_t i = primIndex(t);
  if (!m_primReady[i]) {
    MeshCPU cpu = makePrimitivePN(t, 32);
    m_primMeshes[i].upload(cpu);
    m_primReady[i] = true;
  }
  m_primMeshes[i].drawBaseInstance(baseInstance);
}

uint32_t Renderer::renderFrame(const RenderPassContext &ctx, bool editorVisible,
                               const RenderableRegistry &registry,
                               const std::vector<uint32_t> &selectedPickIDs,
                               EngineContext &engine) {
  // Selection SSBO for outline
  setSelectedPickIDs(selectedPickIDs, engine.selectedActivePick());

  // Begin RG frame
  m_graph.reset();
  m_rgRes.beginFrame(ctx.frameIndex, ctx.fbWidth, ctx.fbHeight);

  const RenderTextureDesc depthDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc hizDesc{
      .format = RGFormat::R32F,
      .usage = RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
      .mipCount = 1,
  };
  const RenderTextureDesc hdrDesc{
      .format = RGFormat::RGBA16F,
      .usage =
          RGTexUsage::ColorAttach | RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc hdrDebugDesc{
      .format = RGFormat::RGBA16F,
      .usage = RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc hdrOitDesc{
      .format = RGFormat::RGBA16F,
      .usage = RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc idDesc{
      .format = RGFormat::R32UI,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc oitAccumDesc{
      .format = RGFormat::RGBA16F,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc oitRevealDesc{
      .format = RGFormat::RGBA16F,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc postInDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc ldrDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled | RGTexUsage::Image,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc maskDesc{
      .format = RGFormat::R32UI,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc outDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Framebuffer, 0, 0},
  };
  const RenderTextureDesc shadowCSMDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 2048, 2048},
  };
  const RenderTextureDesc previewColorDesc{
      .format = RGFormat::RGBA8,
      .usage = RGTexUsage::ColorAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 256, 256},
  };
  const RenderTextureDesc previewDepthDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 256, 256},
  };

  m_graph.declareTexture("Depth.Pre", depthDesc);
  const uint32_t maxDim = std::max(ctx.fbWidth, ctx.fbHeight);
  uint32_t hizMips = 1;
  for (uint32_t v = maxDim; v > 1; v >>= 1)
    ++hizMips;
  RenderTextureDesc hizDescResolved = hizDesc;
  hizDescResolved.mipCount = hizMips;
  m_graph.declareTexture("HiZ.Depth", hizDescResolved);
  m_graph.declareTexture("HDR.Color", hdrDesc);
  m_graph.declareTexture("HDR.Debug", hdrDebugDesc);
  m_graph.declareTexture("HDR.OIT", hdrOitDesc);
  m_graph.declareTexture("ID.Submesh", idDesc);
  m_graph.declareTexture("ID.Pick", idDesc);
  m_graph.declareTexture("Depth.Pick", depthDesc);
  m_graph.declareTexture("Trans.Accum", oitAccumDesc);
  m_graph.declareTexture("Trans.Reveal", oitRevealDesc);
  m_graph.declareTexture("Post.In", postInDesc);
  m_graph.declareTexture("LDR.Color", ldrDesc);
  m_graph.declareTexture("LDR.Temp", ldrDesc);
  m_graph.declareTexture("Mask.SelectedTrans", maskDesc);
  m_graph.declareTexture("OUT.Color", outDesc);
  m_graph.declareTexture("Preview.Material", previewColorDesc);
  m_graph.declareTexture("Preview.MaterialDepth", previewDepthDesc);

  // Shadow atlas textures
  // 1. CSM Atlas: 4 cascades of primary directional light
  const RenderTextureDesc shadowCSMAtlasDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 4096u, 4096u},
  };
  m_graph.declareTexture("Shadow.CSMAtlas", shadowCSMAtlasDesc);

  // 2. Spot Light Atlas: All spot lights packed into single atlas
  const RenderTextureDesc shadowSpotAtlasDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 2048u, 2048u},
  };
  m_graph.declareTexture("Shadow.SpotAtlas", shadowSpotAtlasDesc);

  // 3. Dir Light Atlas: Additional directional lights (non-cascaded)
  const RenderTextureDesc shadowDirAtlasDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 2048u, 2048u},
  };
  m_graph.declareTexture("Shadow.DirAtlas", shadowDirAtlasDesc);

  // 4. Point Light Cubemap Array: One cubemap per point light (6 faces each)
  constexpr uint32_t kMaxPointLights = 16u;
  const RenderTextureDesc shadowPointArrayDesc{
      .format = RGFormat::Depth32F,
      .usage = RGTexUsage::DepthAttach | RGTexUsage::Sampled,
      .extent = {RenderExtentKind::Explicit, 512u, 512u},
      .layers = kMaxPointLights * 6u,
  };
  m_graph.declareTexture("Shadow.PointArray", shadowPointArrayDesc);

  const uint32_t tileSize = 16;
  const uint32_t tilesX = (ctx.fbWidth + tileSize - 1) / tileSize;
  const uint32_t tilesY = (ctx.fbHeight + tileSize - 1) / tileSize;
  const uint32_t tileCount = tilesX * tilesY;
  const uint32_t zSlices = 16;
  const uint32_t maxPerCluster = 96;
  const uint32_t clusterCount = tileCount * zSlices;

  const RGBufferDesc headerDesc{
      .byteSize = clusterCount * 8u,
      .usage = RGBufferUsage::SSBO,
      .dynamic = true,
  };
  const RGBufferDesc indicesDesc{
      .byteSize = clusterCount * maxPerCluster * 4u,
      .usage = RGBufferUsage::SSBO,
      .dynamic = true,
  };
  const RGBufferDesc metaDesc{
      .byteSize = 64u,
      .usage = RGBufferUsage::UBO,
      .dynamic = true,
  };

  m_graph.declareBuffer("LightGrid.Header", headerDesc);
  m_graph.declareBuffer("LightGrid.Indices", indicesDesc);
  m_graph.declareBuffer("LightGrid.Meta", metaDesc);
  m_graph.declareBuffer("Scene.Lights",
                        RGBufferDesc{.byteSize = 1u,
                                     .usage = RGBufferUsage::SSBO,
                                     .dynamic = true});
  m_graph.declareBuffer("Scene.PerDraw",
                        RGBufferDesc{.byteSize = 1u,
                                     .usage = RGBufferUsage::SSBO,
                                     .dynamic = true});
  m_graph.declareBuffer("Post.Filters",
                        RGBufferDesc{.byteSize = 1u,
                                     .usage = RGBufferUsage::SSBO,
                                     .dynamic = true});

  {
    auto &bb = m_graph.blackboard();
    const RGBufferRef lightsRef = bb.getBuffer("Scene.Lights");
    if (lightsRef != InvalidRGBuffer) {
      GLBuffer external{};
      external.buf = engine.lights().ssbo();
      external.byteSize = 0;
      bb.bindExternalBuffer(lightsRef, external);
    }

    const RGBufferRef perDrawRef = bb.getBuffer("Scene.PerDraw");
    if (perDrawRef != InvalidRGBuffer) {
      GLBuffer external{};
      external.buf = engine.perDraw().ssbo();
      external.byteSize = 0;
      bb.bindExternalBuffer(perDrawRef, external);
    }

    const RGBufferRef filtersRef = bb.getBuffer("Post.Filters");
    if (filtersRef != InvalidRGBuffer) {
      GLBuffer external{};
      external.buf = engine.postFiltersSSBO();
      external.byteSize = 0;
      bb.bindExternalBuffer(filtersRef, external);
    }
  }

  m_passEnvEquirect.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passEnvIrradiance.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passEnvPrefilter.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passEnvBRDF.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPreview.setup(m_graph, ctx, registry, engine, editorVisible);

  m_passShadowCSM.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passShadowSpot.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passShadowDir.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passShadowPoint.setup(m_graph, ctx, registry, engine, editorVisible);
  m_graph.addPass(
      "ShadowMetadataUpload",
      [&](RenderPassBuilder &b) {
        b.readTexture("Shadow.SpotAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.DirAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.PointArray", RenderAccess::SampledRead);
        b.writeBuffer("Scene.Lights", RenderAccess::SSBOWrite);
      },
      [&](const RenderPassContext &, RenderResourceBlackboard &,
          RGResources &) {
        engine.lights().updateShadowMetadata(m_passShadowSpot, m_passShadowDir,
                                             m_passShadowPoint);
      });

  m_passDepthPre.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passHiZ.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passLightCluster.setLightCount(engine.lights().lightCount());
  m_passLightCluster.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passLightGridDebug.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPickID.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passForwardOpaque.setMode(PassForwardMRT::Mode::Opaque);
  m_passForwardOpaque.setup(m_graph, ctx, registry, engine, editorVisible);

  const bool useOIT =
      (engine.transparencyMode() == TransparencyMode::OIT);

  m_passSky.setup(m_graph, ctx, registry, engine, editorVisible);

  if (useOIT) {
    m_passTransparentOIT.setup(m_graph, ctx, registry, engine, editorVisible);
    m_passTransparentOITComposite.setup(m_graph, ctx, registry, engine,
                                        editorVisible);
  } else {
    m_passForwardTransparent.setMode(PassForwardMRT::Mode::Transparent);
    m_passForwardTransparent.setup(m_graph, ctx, registry, engine,
                                   editorVisible);
  }

  m_passSelectionMaskTransparent.setup(m_graph, ctx, registry, engine,
                                       editorVisible);
  m_passShadowDebug.setMode(engine.shadowDebugMode());
  m_passShadowDebug.setOverlayAlpha(engine.shadowDebugAlpha());
  m_passShadowDebug.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passTonemap.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPost.setSSBO(engine.postFiltersSSBO());
  m_passPost.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passSelection.setup(m_graph, ctx, registry, engine, editorVisible);
  m_passPresent.setup(m_graph, ctx, registry, engine, editorVisible);

  m_graph.execute(ctx, m_rgRes);

  auto &bb = m_graph.blackboard();
  m_out.hdr = bb.textureHandle(bb.getTexture("HDR.Debug"));
  m_out.id = bb.textureHandle(bb.getTexture("ID.Submesh"));
  m_out.pick = bb.textureHandle(bb.getTexture("ID.Pick"));
  m_out.depth = bb.textureHandle(bb.getTexture("Depth.Pre"));
  m_out.ldr = bb.textureHandle(bb.getTexture("LDR.Color"));
  m_out.outlined = bb.textureHandle(bb.getTexture("OUT.Color"));
  m_out.preview = bb.textureHandle(bb.getTexture("Preview.Material"));

  const auto &finalT = m_rgRes.tex(m_out.outlined);
  return finalT.tex;
}

uint32_t Renderer::previewTexture() const {
  if (m_out.preview == InvalidRG)
    return 0u;
  return m_rgRes.tex(m_out.preview).tex;
}

uint32_t Renderer::readPickID(uint32_t px, uint32_t py,
                              uint32_t fbHeight) const {
  if (m_out.pick == InvalidRG && m_out.id == InvalidRG)
    return 0u;
  if (fbHeight == 0u)
    return 0u;

  const uint32_t glY = (fbHeight - 1u) > py ? (fbHeight - 1u - py) : 0u;

  uint32_t id = 0;
  const auto &idT =
      (m_out.pick != InvalidRG) ? m_rgRes.tex(m_out.pick) : m_rgRes.tex(m_out.id);

  static uint32_t s_readFbo = 0;
  if (s_readFbo == 0)
    glCreateFramebuffers(1, &s_readFbo);

  glNamedFramebufferTexture(s_readFbo, GL_COLOR_ATTACHMENT0, idT.tex, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, s_readFbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);

  glReadPixels((int)px, (int)glY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &id);

  if (id == 0u && m_out.id != InvalidRG && m_out.pick != m_out.id) {
    const auto &fallback = m_rgRes.tex(m_out.id);
    glNamedFramebufferTexture(s_readFbo, GL_COLOR_ATTACHMENT0, fallback.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, s_readFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels((int)px, (int)glY, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &id);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return id;
}

} // namespace Nyx
