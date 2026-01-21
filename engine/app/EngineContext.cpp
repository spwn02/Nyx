#include "EngineContext.h"

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

#include "../core/Log.h"
#include "scene/Pick.h"

#include <glad/glad.h>

namespace Nyx {

EngineContext::EngineContext() { m_materials.initGL(); }
EngineContext::~EngineContext() { m_materials.shutdownGL(); }

void EngineContext::tick(float dt) {
  m_time += dt;
  m_dt = dt;
  m_materials.uploadIfDirty();
}

void EngineContext::buildRenderables() {
  m_renderables.clear();
  m_world.updateTransforms();

  for (EntityID e : m_world.aliveEntities()) {
    if (!m_world.hasMesh(e))
      continue;

    auto &mc = m_world.mesh(e);
    const glm::mat4 W = m_world.worldTransform(e).world;

    const uint32_t n = (uint32_t)mc.submeshes.size();
    for (uint32_t si = 0; si < n; ++si) {
      auto &sm = mc.submeshes[si];

      if (sm.material == InvalidMaterial || !m_materials.isAlive(sm.material)) {
        MaterialData def{};
        sm.material = m_materials.create(def);
      }

      const uint32_t matIdx = m_materials.gpuIndex(sm.material);

      auto &r = m_renderables.create(e);
      r.submesh = si;
      r.pickID = packPick(e, si);
      r.mesh = sm.type;
      r.model = W;
      r.materialGpuIndex = matIdx;
    }
  }
}

uint32_t EngineContext::render(uint32_t fbWidth, uint32_t fbHeight,
                               EditorCamera &camera, bool editorVisible) {
  camera.setViewport(fbWidth, fbHeight);

  RenderContext ctx{};
  ctx.fbWidth = fbWidth;
  ctx.fbHeight = fbHeight;

  camera.setViewport(fbWidth, fbHeight);
  camera.updateIfDirty();
  ctx.viewProj = camera.viewProj();

  buildRenderables();

  // Outline: selected pickIDs come straight from editor selection
  m_renderer.setSelectedPickIDs(m_selectedPickIDs);

  uint32_t outTex = m_renderer.renderFrame(ctx, editorVisible, m_renderables,
                                           m_selectedPickIDs, *this);

  if (m_pickRequested) {
    m_lastPickedID = m_renderer.readPickID(m_pickX, m_pickY, ctx.fbHeight);
    m_pickRequested = false;
  }

  return outTex;
}

} // namespace Nyx
