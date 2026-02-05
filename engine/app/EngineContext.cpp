#include "EngineContext.h"

#include "render/passes/PassShadowCSM.h"
#include "render/filters/LUT3DLoader.h"
#include "render/rg/RenderPassContext.h"
#include "scene/material/MaterialData.h"

#include <glad/glad.h>
#include <algorithm>
#include <vector>
#include <glm/gtc/matrix_inverse.hpp>

namespace Nyx {

EngineContext::EngineContext() {
  m_materials.initGL(m_renderer.resources());
  m_lights.initGL();
  m_envIBL.init(m_renderer.shaders());

  // Create Sky UBO
  glCreateBuffers(1, &m_skyUBO);
  glNamedBufferData(m_skyUBO, sizeof(SkyConstants), nullptr, GL_DYNAMIC_DRAW);

  // Create Shadow CSM UBO
  glCreateBuffers(1, &m_shadowCSMUBO);
  glNamedBufferData(m_shadowCSMUBO, sizeof(ShadowCSMUBO), nullptr,
                    GL_DYNAMIC_DRAW);

  // Create material texture remap SSBO
  glCreateBuffers(1, &m_texRemapSSBO);
  glNamedBufferData(m_texRemapSSBO, 0, nullptr, GL_DYNAMIC_DRAW);

  // Post-filter graph + SSBO (requires GL)
  initPostFilters();
}

EngineContext::~EngineContext() {
  if (m_skyUBO) {
    glDeleteBuffers(1, &m_skyUBO);
    m_skyUBO = 0;
  }
  if (m_shadowCSMUBO) {
    glDeleteBuffers(1, &m_shadowCSMUBO);
    m_shadowCSMUBO = 0;
  }
  if (m_texRemapSSBO) {
    glDeleteBuffers(1, &m_texRemapSSBO);
    m_texRemapSSBO = 0;
  }
  if (m_postLUT3D) {
    glDeleteTextures(1, &m_postLUT3D);
    m_postLUT3D = 0;
  }
  for (uint32_t t : m_postLUTs) {
    if (t)
      glDeleteTextures(1, &t);
  }
  m_postLUTs.clear();
  m_postLUTIndex.clear();
  m_filterStack.shutdown();
  m_lights.shutdownGL();
  m_materials.shutdownGL();
  m_envIBL.shutdown();
}

void EngineContext::tick(float dt) {
  m_time += dt;
  m_dt = dt;
  m_materials.processTextureUploads(8);
  m_materials.uploadIfDirty();
}

void EngineContext::buildRenderables() {
  m_world.updateTransforms();
  m_renderables.applyEvents(m_world, m_world.events());
}

uint32_t EngineContext::materialIndex(const Renderable &r) {
  if (!m_world.hasMesh(r.entity))
    return 0u;
  auto &mc = m_world.mesh(r.entity);
  if (r.submesh >= mc.submeshes.size())
    return 0u;

  auto &sm = mc.submeshes[r.submesh];
  if (sm.material == InvalidMaterial || !m_materials.isAlive(sm.material)) {
    MaterialData def{};
    sm.material = m_materials.create(def);
  }
  return m_materials.gpuIndex(sm.material);
}

void EngineContext::rebuildRenderables() {
  m_world.updateTransforms();
  m_renderables.rebuildAll(m_world);
}

void EngineContext::resetMaterials() { m_materials.reset(); }

void EngineContext::rebuildEntityIndexMap() {
  m_entityByIndex.clear();
  for (EntityID e : m_world.alive()) {
    if (!m_world.isAlive(e))
      continue;
    m_entityByIndex[e.index] = e;
  }
}

uint32_t EngineContext::render(uint32_t windowWidth, uint32_t windowHeight,
                               uint32_t viewportWidth, uint32_t viewportHeight,
                               uint32_t fbWidth, uint32_t fbHeight,
                               bool editorVisible) {
  const auto &events = m_world.events().events();
  for (const auto &e : events) {
    handleWorldEvent(e);
  }

  buildRenderables();
  m_envIBL.ensureResources();

  for (const auto &r : m_renderables.all()) {
    (void)materialIndex(r);
  }

  EntityID camEnt = m_renderCameraOverride;
  if (camEnt == InvalidEntity || !m_world.hasCamera(camEnt))
    camEnt = m_world.activeCamera();
  const bool hasCam = (camEnt != InvalidEntity && m_world.hasCamera(camEnt));

  if (fbWidth != m_lastFbWidth || fbHeight != m_lastFbHeight) {
    if (hasCam) {
      m_world.camera(camEnt).dirty = true;
    }
    m_lastFbWidth = fbWidth;
    m_lastFbHeight = fbHeight;
  }

  m_cameras.update(m_world, fbWidth, fbHeight);
  const CCameraMatrices *mats =
      hasCam ? &m_world.cameraMatrices(camEnt) : nullptr;

  RenderPassContext ctx{};
  ctx.windowWidth = windowWidth;
  ctx.windowHeight = windowHeight;
  ctx.viewportWidth = viewportWidth;
  ctx.viewportHeight = viewportHeight;
  ctx.fbWidth = fbWidth;
  ctx.fbHeight = fbHeight;
  ctx.frameIndex = m_frameIndex++;
  if (mats) {
    ctx.view = mats->view;
    ctx.proj = mats->proj;
    ctx.viewProj = mats->viewProj;

    const glm::mat4 camWorld = m_world.worldTransform(camEnt).world;
    ctx.cameraPos = glm::vec3(camWorld[3]);
    glm::vec3 forward = glm::normalize(glm::vec3(camWorld[2]));
    ctx.cameraDir = -forward;
    if (m_world.hasCamera(camEnt)) {
      const auto &cam = m_world.camera(camEnt);
      setCameraCache(mats->view, mats->proj, cam.nearZ, cam.farZ);
    }
  }

  // Old shadow system disabled - now using new atlas-based system via Renderer
  // m_shadows.render(*this, m_renderables, ctx,
  //                  [this](ProcMeshType t) { m_renderer.drawPrimitive(t); });

  m_lights.updateFromWorld(m_world);

  // Update Sky UBO before rendering
  updateSkyUBO(ctx);

  // Update post filter stack SSBO if graph changed
  updatePostFilters();

  // Outline: selected pickIDs come straight from editor selection
  m_renderer.setSelectedPickIDs(m_selectedPickIDs);

  uint32_t outTex = m_renderer.renderFrame(ctx, editorVisible, m_renderables,
                                           m_selectedPickIDs, *this);

  if (m_pickRequested) {
    m_lastPickedID = m_renderer.readPickID(m_pickX, m_pickY, ctx.fbHeight);
    m_pickRequested = false;
  }

  m_world.clearEvents();

  return outTex;
}

ShadowCSMConfig &EngineContext::shadowCSMConfig() {
  return m_renderer.shadowCSMConfig();
}

const ShadowCSMConfig &EngineContext::shadowCSMConfig() const {
  return m_renderer.shadowCSMConfig();
}

EntityID EngineContext::resolveEntityIndex(uint32_t index) const {
  auto it = m_entityByIndex.find(index);
  return (it == m_entityByIndex.end()) ? InvalidEntity : it->second;
}

void EngineContext::handleWorldEvent(const WorldEvent &e) {
  switch (e.type) {
  case WorldEventType::EntityCreated:
    m_entityByIndex[e.a.index] = e.a;
    break;
  case WorldEventType::EntityDestroyed:
    m_entityByIndex.erase(e.a.index);
    break;
  case WorldEventType::SkyChanged: {
    // Rebuild IBL when sky HDRI path changes
    const auto &sky = m_world.skySettings();
    if (!sky.hdriPath.empty()) {
      m_envIBL.loadFromHDR(sky.hdriPath);
    }
    break;
  }
  default:
    break;
  }
}

void EngineContext::updateSkyUBO(const RenderPassContext &ctx) {
  // Fill Sky UBO
  m_sky.invViewProj = glm::inverse(ctx.viewProj);
  m_sky.camPos = glm::vec4(ctx.cameraPos, 0.0f);

  const auto &sky = m_world.skySettings();
  float intensity = sky.enabled ? sky.intensity : 0.0f;
  float exposureStops = sky.exposure;
  float yawDeg = sky.rotationYawDeg;
  float drawBg = (sky.enabled && sky.drawBackground) ? 1.0f : 0.0f;

  float yawRad = glm::radians(yawDeg);
  m_sky.skyParams = glm::vec4(intensity, exposureStops, yawRad, drawBg);
  m_sky.skyParams2 = glm::vec4(std::max(0.0f, sky.ambient), 0, 0, 0);

  // Upload to GPU
  glNamedBufferSubData(m_skyUBO, 0, sizeof(SkyConstants), &m_sky);

  // Bind to binding point 2
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_skyUBO);
}

void EngineContext::initPostFilters() {
  m_filterRegistry.clear();
  m_filterRegistry.registerBuiltins();
  m_filterRegistry.finalize();

  m_filterStack.init(m_filterRegistry);

  // Create identity 3D LUT (16x16x16)
  if (!m_postLUT3D) {
    glCreateTextures(GL_TEXTURE_3D, 1, &m_postLUT3D);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(m_postLUT3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  }
  const int lutSize = 16;
  std::vector<float> lut;
  lut.resize((size_t)lutSize * lutSize * lutSize * 3u);
  for (int b = 0; b < lutSize; ++b) {
    for (int g = 0; g < lutSize; ++g) {
      for (int r = 0; r < lutSize; ++r) {
        const float rf = (float)r / (float)(lutSize - 1);
        const float gf = (float)g / (float)(lutSize - 1);
        const float bf = (float)b / (float)(lutSize - 1);
        const size_t idx =
            ((size_t)b * lutSize * lutSize + (size_t)g * lutSize + (size_t)r) *
            3u;
        lut[idx + 0] = rf;
        lut[idx + 1] = gf;
        lut[idx + 2] = bf;
      }
    }
  }
  glTextureStorage3D(m_postLUT3D, 1, GL_RGB16F, lutSize, lutSize, lutSize);
  glTextureSubImage3D(m_postLUT3D, 0, 0, 0, 0, lutSize, lutSize, lutSize,
                      GL_RGB, GL_FLOAT, lut.data());
  m_postLUTs.clear();
  m_postLUTIndex.clear();
  m_postLUTs.push_back(m_postLUT3D);
  m_postLUTPaths.clear();
  m_postLUTPaths.emplace_back(""); // identity
  m_postLUTSizes.clear();
  m_postLUTSizes.push_back(lutSize);

  // Seed default editor graph (Input -> Exposure -> Contrast -> Saturation -> Output)
  m_postGraph = PostGraph();
  const FilterNode exp = m_filterRegistry.makeNode(1);
  const FilterNode con = m_filterRegistry.makeNode(2);
  const FilterNode sat = m_filterRegistry.makeNode(3);

  auto defaultsFrom = [](const FilterRegistry &reg, FilterTypeId id) {
    std::vector<float> out;
    const FilterTypeInfo *t = reg.find(id);
    if (!t)
      return out;
    out.reserve(t->paramCount);
    for (uint32_t i = 0; i < t->paramCount; ++i)
      out.push_back(t->params[i].defaultValue);
    return out;
  };

  m_postGraph.addFilter(1, exp.label.c_str(), defaultsFrom(m_filterRegistry, 1));
  m_postGraph.addFilter(2, con.label.c_str(), defaultsFrom(m_filterRegistry, 2));
  m_postGraph.addFilter(3, sat.label.c_str(), defaultsFrom(m_filterRegistry, 3));

  m_postGraphDirty = true;
  syncFilterGraphFromPostGraph();
}

void EngineContext::updatePostFilters() {
  if (m_postGraphDirty)
    syncFilterGraphFromPostGraph();
  m_filterStack.updateIfDirty(m_filterGraph);
}

uint32_t EngineContext::postLUTTexture(uint32_t idx) const {
  if (idx >= m_postLUTs.size())
    return m_postLUT3D;
  return m_postLUTs[idx];
}

uint32_t EngineContext::postLUTSize(uint32_t idx) const {
  if (idx >= m_postLUTSizes.size())
    return 0;
  return m_postLUTSizes[idx];
}

uint32_t EngineContext::ensurePostLUT3D(const std::string &path) {
  if (path.empty())
    return 0u;
  auto it = m_postLUTIndex.find(path);
  if (it != m_postLUTIndex.end())
    return it->second;

  if (m_postLUTs.size() >= 8) {
    // cap at 8 LUTs (including identity)
    return 0u;
  }

  LUT3DData data{};
  std::string err;
  if (!loadCubeLUT3D(path, data, err))
    return 0u;

  uint32_t tex = 0;
  glCreateTextures(GL_TEXTURE_3D, 1, &tex);
  glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTextureStorage3D(tex, 1, GL_RGB16F, (GLsizei)data.size,
                     (GLsizei)data.size, (GLsizei)data.size);
  glTextureSubImage3D(tex, 0, 0, 0, 0, (GLsizei)data.size,
                      (GLsizei)data.size, (GLsizei)data.size, GL_RGB, GL_FLOAT,
                      data.rgb.data());

  const uint32_t idx = (uint32_t)m_postLUTs.size();
  m_postLUTs.push_back(tex);
  m_postLUTPaths.push_back(path);
  m_postLUTSizes.push_back(data.size);
  m_postLUTIndex.emplace(path, idx);
  return idx;
}

bool EngineContext::reloadPostLUT3D(const std::string &path) {
  if (path.empty())
    return false;
  auto it = m_postLUTIndex.find(path);
  if (it == m_postLUTIndex.end())
    return false;
  const uint32_t idx = it->second;
  if (idx == 0 || idx >= m_postLUTs.size())
    return false;

  LUT3DData data{};
  std::string err;
  if (!loadCubeLUT3D(path, data, err))
    return false;

  uint32_t &tex = m_postLUTs[idx];
  if (m_postLUTSizes[idx] != data.size) {
    if (tex)
      glDeleteTextures(1, &tex);
    glCreateTextures(GL_TEXTURE_3D, 1, &tex);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTextureStorage3D(tex, 1, GL_RGB16F, (GLsizei)data.size,
                       (GLsizei)data.size, (GLsizei)data.size);
  }

  glTextureSubImage3D(tex, 0, 0, 0, 0, (GLsizei)data.size,
                      (GLsizei)data.size, (GLsizei)data.size, GL_RGB, GL_FLOAT,
                      data.rgb.data());
  m_postLUTSizes[idx] = data.size;
  return true;
}

bool EngineContext::clearPostLUT(uint32_t idx) {
  if (idx == 0 || idx >= m_postLUTs.size())
    return false;
  uint32_t &tex = m_postLUTs[idx];
  if (tex) {
    glDeleteTextures(1, &tex);
    tex = m_postLUT3D;
  }
  if (idx < m_postLUTPaths.size())
    m_postLUTPaths[idx].clear();
  if (idx < m_postLUTSizes.size())
    m_postLUTSizes[idx] = m_postLUTSizes[0];
  for (auto it = m_postLUTIndex.begin(); it != m_postLUTIndex.end();) {
    if (it->second == idx)
      it = m_postLUTIndex.erase(it);
    else
      ++it;
  }
  return true;
}

void EngineContext::syncFilterGraphFromPostGraph() {
  std::vector<PGNodeID> order;
  const PGCompileError err = m_postGraph.buildChainOrder(order);
  if (!err.ok) {
    m_filterGraph.clear();
    m_postGraphDirty = false;
    return;
  }

  m_filterGraph.clear();
  for (PGNodeID id : order) {
    PGNode *n = m_postGraph.findNode(id);
    if (!n || n->kind != PGNodeKind::Filter)
      continue;

    const FilterTypeInfo *ti =
        m_filterRegistry.find(static_cast<FilterTypeId>(n->typeID));
    if (!ti)
      continue;

    FilterNode fn = m_filterRegistry.makeNode(ti->id);
    fn.enabled = n->enabled;
    fn.label = n->name;

    const uint32_t pc =
        std::min<uint32_t>(ti->paramCount, FilterNode::kMaxParams);
    for (uint32_t i = 0; i < pc; ++i) {
      if (i < n->params.size())
        fn.params[i] = n->params[i];
    }
    if (ti->name && std::string(ti->name) == "LUT") {
      const uint32_t lutIdx = ensurePostLUT3D(n->lutPath);
      if (pc > 1)
        fn.params[1] = (float)lutIdx;
    }

    m_filterGraph.addNode(std::move(fn));
  }

  m_postGraphDirty = false;
}

} // namespace Nyx
