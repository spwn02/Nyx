#include "EngineContext.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

#include "../core/Log.h"
#include "scene/MaterialData.h"
#include "render/rg/RenderPassContext.h"

#include <glad/glad.h>

namespace Nyx {

EngineContext::EngineContext() {
  m_materials.initGL();
  m_lights.initGL();
  m_shadows.initGL();
}
EngineContext::~EngineContext() {
  m_shadows.shutdownGL();
  m_lights.shutdownGL();
  m_materials.shutdownGL();
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

  m_shadows.render(*this, m_renderables, ctx,
                   [this](ProcMeshType t) { m_renderer.drawPrimitive(t); });

  m_lights.updateFromWorld(m_world, &m_shadows);

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
  default:
    break;
  }
}

} // namespace Nyx
