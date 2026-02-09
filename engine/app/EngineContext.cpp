#include "EngineContext.h"

#include "render/gl/GLResources.h"

#include <glad/glad.h>

namespace Nyx {

EngineContext::EngineContext() {
  m_materials.initGL(m_renderer.resources());
  m_lights.initGL();
  m_envIBL.init(m_renderer.shaders());

  glCreateBuffers(1, &m_skyUBO);
  glNamedBufferData(m_skyUBO, sizeof(SkyConstants), nullptr, GL_DYNAMIC_DRAW);

  glCreateBuffers(1, &m_shadowCSMUBO);
  glNamedBufferData(m_shadowCSMUBO, sizeof(ShadowCSMUBO), nullptr,
                    GL_DYNAMIC_DRAW);

  glCreateBuffers(1, &m_texRemapSSBO);
  glNamedBufferData(m_texRemapSSBO, 0, nullptr, GL_DYNAMIC_DRAW);

  initPostFilters();

  m_animation.setWorld(&m_world);
  m_animation.setActiveClip(&m_animationClip);
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
  m_perDraw.shutdown();
  m_lights.shutdownGL();
  m_materials.shutdownGL();
  m_envIBL.shutdown();
}

void EngineContext::tick(float dt) {
  m_time += dt;
  m_dt = dt;
  m_materials.processTextureUploads(8);
  m_materials.uploadIfDirty();
  m_animation.tick(dt);
}

void EngineContext::setHiddenEntities(const std::vector<EntityID> &ents) {
  m_hiddenEntities.clear();
  m_hiddenEntities.reserve(ents.size());
  for (EntityID e : ents)
    m_hiddenEntities.insert(e);
}

} // namespace Nyx
