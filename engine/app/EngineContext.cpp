#include "EngineContext.h"

#include "render/passes/PassShadowCSM.h"
#include "render/rg/RenderPassContext.h"
#include "scene/material/MaterialData.h"

#include <glad/glad.h>
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
  m_lights.shutdownGL();
  m_materials.shutdownGL();
  m_envIBL.shutdown();
}

void EngineContext::tick(float dt) {
  m_time += dt;
  m_dt = dt;
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

void EngineContext::resetMaterials() {
  m_materials.reset();
}

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

  // Upload to GPU
  glNamedBufferSubData(m_skyUBO, 0, sizeof(SkyConstants), &m_sky);

  // Bind to binding point 2
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_skyUBO);
}

} // namespace Nyx
