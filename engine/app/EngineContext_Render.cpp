#include "EngineContext.h"

#include "core/Assert.h"
#include "render/draw/DrawData.h"
#include "render/passes/PassShadowCSM.h"
#include "render/rg/RenderPassContext.h"
#include "scene/material/MaterialData.h"

#include <algorithm>
#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <vector>

namespace Nyx {

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
  for (const auto &e : events)
    handleWorldEvent(e);

  buildRenderables();
  m_envIBL.ensureResources();

  for (auto &r : m_renderables.allMutable()) {
    const uint32_t idx = materialIndex(r);
    r.materialGpuIndex = idx;
    if (m_world.hasMesh(r.entity) &&
        r.submesh < m_world.mesh(r.entity).submeshes.size()) {
      const auto &sm = m_world.mesh(r.entity).submeshes[r.submesh];
      if (sm.material != InvalidMaterial && m_materials.isAlive(sm.material)) {
        r.alphaMode = m_materials.alphaMode(sm.material);
      } else {
        r.alphaMode = MatAlphaMode::Opaque;
      }
    } else {
      r.alphaMode = MatAlphaMode::Opaque;
    }
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

  m_renderables.buildRoutedLists(ctx.cameraPos, ctx.cameraDir);

  {
    const auto &opaque = m_renderables.opaque();
    const auto &transparent = m_renderables.transparentSorted();

    std::vector<DrawData> draws;
    draws.reserve(opaque.size() + transparent.size());

    auto pushDraw = [&](const Renderable &r) {
      DrawData d{};
      d.model = r.model;
      d.materialIndex = r.materialGpuIndex;
      d.pickID = r.pickID;
      d.meshHandle = static_cast<uint32_t>(r.mesh);
      draws.push_back(d);
    };

    m_perDrawOpaqueOffset = 0;
    for (const auto &r : opaque) {
      if (isEntityHidden(r.entity))
        continue;
      pushDraw(r);
    }
    m_perDrawOpaqueCount = static_cast<uint32_t>(draws.size());

    m_perDrawTransparentOffset = static_cast<uint32_t>(draws.size());
    for (const auto &r : transparent) {
      if (isEntityHidden(r.entity))
        continue;
      pushDraw(r);
    }
    m_perDrawTransparentCount =
        static_cast<uint32_t>(draws.size() - m_perDrawTransparentOffset);

    m_perDraw.upload(draws);
  }

  m_lights.updateFromWorld(m_world);

  updateSkyUBO(ctx);
  updatePostFilters();

  m_renderer.setSelectedPickIDs(m_selectedPickIDs, m_selectedActivePick);

  m_lastPreviewCaptureTex = 0;
  MaterialHandle prevPreview = m_previewMaterial;
  if (!m_previewCaptureQueue.empty()) {
    m_activePreviewCapture = m_previewCaptureQueue.front();
    m_previewCaptureQueue.erase(m_previewCaptureQueue.begin());
    if (m_activePreviewCapture.mat != InvalidMaterial)
      m_previewMaterial = m_activePreviewCapture.mat;
  } else {
    m_activePreviewCapture = {};
  }

  uint32_t outTex = m_renderer.renderFrame(ctx, editorVisible, m_renderables,
                                           m_selectedPickIDs, *this);

  if (m_activePreviewCapture.mat != InvalidMaterial &&
      m_activePreviewCapture.targetTex != 0) {
    const uint32_t srcTex = m_renderer.previewTexture();
    if (srcTex != 0) {
      int srcW = 0, srcH = 0;
      int dstW = 0, dstH = 0;
      glGetTextureLevelParameteriv(srcTex, 0, GL_TEXTURE_WIDTH, &srcW);
      glGetTextureLevelParameteriv(srcTex, 0, GL_TEXTURE_HEIGHT, &srcH);
      glGetTextureLevelParameteriv(m_activePreviewCapture.targetTex, 0,
                                   GL_TEXTURE_WIDTH, &dstW);
      glGetTextureLevelParameteriv(m_activePreviewCapture.targetTex, 0,
                                   GL_TEXTURE_HEIGHT, &dstH);
      if (srcW > 0 && srcH > 0 && dstW > 0 && dstH > 0) {
        static GLuint s_readFbo = 0;
        static GLuint s_drawFbo = 0;
        if (s_readFbo == 0)
          glCreateFramebuffers(1, &s_readFbo);
        if (s_drawFbo == 0)
          glCreateFramebuffers(1, &s_drawFbo);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, s_readFbo);
        glNamedFramebufferTexture(s_readFbo, GL_COLOR_ATTACHMENT0, srcTex, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_drawFbo);
        glNamedFramebufferTexture(s_drawFbo, GL_COLOR_ATTACHMENT0,
                                  m_activePreviewCapture.targetTex, 0);

        glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        m_lastPreviewCaptureTex = m_activePreviewCapture.targetTex;
      }
    }
  }

  m_previewMaterial = prevPreview;

  if (m_pickRequested) {
    m_lastPickedID = m_renderer.readPickID(m_pickX, m_pickY, ctx.fbHeight);
    m_pickRequested = false;
  }

  m_world.clearEvents();

  return outTex;
}

void EngineContext::requestMaterialPreview(MaterialHandle h,
                                           uint32_t targetTex) {
  if (h == InvalidMaterial || targetTex == 0)
    return;
  m_previewCaptureQueue.push_back({h, targetTex});
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

void EngineContext::rendererDrawPrimitive(uint32_t meshHandle,
                                          uint32_t baseInstance) {
  NYX_ASSERT(meshHandle <= static_cast<uint32_t>(ProcMeshType::Monkey),
             "rendererDrawPrimitive: invalid meshHandle");
  const auto type = static_cast<ProcMeshType>(meshHandle);
  m_renderer.drawPrimitiveBaseInstance(type, baseInstance);
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

  glNamedBufferSubData(m_skyUBO, 0, sizeof(SkyConstants), &m_sky);
  glBindBufferBase(GL_UNIFORM_BUFFER, 2, m_skyUBO);
}

} // namespace Nyx
