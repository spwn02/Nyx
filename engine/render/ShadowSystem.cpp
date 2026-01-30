#include "render/ShadowSystem.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "render/gl/GLFullscreenTriangle.h"
#include "render/rg/RenderPassContext.h"
#include "scene/RenderableRegistry.h"

#include <filesystem>
#include <fstream>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <sstream>

namespace Nyx {

static std::string readTextFile(const std::string &path) {
  auto p = std::filesystem::absolute(path);
  std::ifstream file(p);
  if (!file.is_open()) {
    Log::Error("ShadowSystem: failed to open {}", p.string());
    return "";
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

static glm::vec3 lightDirFromWorld(const glm::mat4 &W) {
  return glm::normalize(-glm::vec3(W[2]));
}

static glm::mat4 spotViewProj(const glm::mat4 &W, float outerAngle, float nearZ,
                              float farZ) {
  const glm::vec3 pos = glm::vec3(W[3]);
  const glm::vec3 dir = lightDirFromWorld(W);
  glm::vec3 up(0.0f, 1.0f, 0.0f);
  if (glm::abs(glm::dot(up, dir)) > 0.95f)
    up = glm::vec3(0.0f, 0.0f, 1.0f);
  const glm::mat4 V = glm::lookAt(pos, pos + dir, up);
  const float fov = glm::clamp(outerAngle * 2.0f, 0.01f, 3.13f);
  const glm::mat4 P = glm::perspective(fov, 1.0f, nearZ, farZ);
  return P * V;
}

static void cubeFaceView(const glm::vec3 &pos, int face, glm::mat4 &outView) {
  static const glm::vec3 dirs[6] = {
      {1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
      {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
  static const glm::vec3 ups[6] = {
      {0, -1, 0}, {0, -1, 0}, {0, 0, 1},
      {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
  outView = glm::lookAt(pos, pos + dirs[face], ups[face]);
}

void ShadowSystem::initGL() {
  if (m_dirFbo != 0)
    return;

  glCreateFramebuffers(1, &m_dirFbo);
  glCreateFramebuffers(1, &m_spotFbo);
  glCreateFramebuffers(1, &m_pointFbo);
  glCreateRenderbuffers(1, &m_pointDepthRbo);

  glCreateBuffers(1, &m_dirMatricesSSBO);
  glCreateBuffers(1, &m_spotMatricesSSBO);

  m_csmSettings.mapSize = m_dirSize;
  m_csmSettings.cascades = 4;

  std::string vs = readTextFile(Paths::shader("shadow_csm_depth.vert"));
  std::string fs = readTextFile(Paths::shader("shadow_csm_depth.frag"));
  std::string pvs = readTextFile(Paths::shader("shadow_point_depth.vert"));
  std::string pfs = readTextFile(Paths::shader("shadow_point_depth.frag"));

  if (!vs.empty() && !fs.empty()) {
    uint32_t v = compileShader(GL_VERTEX_SHADER, vs);
    uint32_t f = compileShader(GL_FRAGMENT_SHADER, fs);
    m_dirProg = linkProgram(v, f);
    glDeleteShader(v);
    glDeleteShader(f);
  }
  if (!pvs.empty() && !pfs.empty()) {
    uint32_t v = compileShader(GL_VERTEX_SHADER, pvs);
    uint32_t f = compileShader(GL_FRAGMENT_SHADER, pfs);
    m_pointProg = linkProgram(v, f);
    glDeleteShader(v);
    glDeleteShader(f);
  }
}

void ShadowSystem::shutdownGL() {
  if (m_dirProg)
    glDeleteProgram(m_dirProg);
  if (m_pointProg)
    glDeleteProgram(m_pointProg);
  m_dirProg = 0;
  m_pointProg = 0;

  if (m_dirFbo)
    glDeleteFramebuffers(1, &m_dirFbo);
  if (m_spotFbo)
    glDeleteFramebuffers(1, &m_spotFbo);
  if (m_pointFbo)
    glDeleteFramebuffers(1, &m_pointFbo);
  if (m_pointDepthRbo)
    glDeleteRenderbuffers(1, &m_pointDepthRbo);
  m_dirFbo = m_spotFbo = m_pointFbo = 0;
  m_pointDepthRbo = 0;

  if (m_dirShadowTex)
    glDeleteTextures(1, &m_dirShadowTex);
  if (m_spotShadowTex)
    glDeleteTextures(1, &m_spotShadowTex);
  if (m_pointShadowTex)
    glDeleteTextures(1, &m_pointShadowTex);
  m_dirShadowTex = m_spotShadowTex = m_pointShadowTex = 0;

  if (m_dirMatricesSSBO)
    glDeleteBuffers(1, &m_dirMatricesSSBO);
  if (m_spotMatricesSSBO)
    glDeleteBuffers(1, &m_spotMatricesSSBO);
  m_dirMatricesSSBO = m_spotMatricesSSBO = 0;
}

void ShadowSystem::ensureDirResources(uint32_t layers) {
  if (layers == 0)
    layers = 1;
  if (m_dirShadowTex && m_dirLayers == layers)
    return;
  if (m_dirShadowTex)
    glDeleteTextures(1, &m_dirShadowTex);

  glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_dirShadowTex);
  glTextureStorage3D(m_dirShadowTex, 1, GL_DEPTH_COMPONENT32F, m_dirSize,
                     m_dirSize, (GLsizei)layers);
  glTextureParameteri(m_dirShadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(m_dirShadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(m_dirShadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTextureParameteri(m_dirShadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTextureParameterfv(m_dirShadowTex, GL_TEXTURE_BORDER_COLOR, border);
  m_dirLayers = layers;
}

void ShadowSystem::ensureSpotResources(uint32_t layers) {
  if (layers == 0)
    layers = 1;
  if (m_spotShadowTex && m_spotLayers == layers)
    return;
  if (m_spotShadowTex)
    glDeleteTextures(1, &m_spotShadowTex);

  glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_spotShadowTex);
  glTextureStorage3D(m_spotShadowTex, 1, GL_DEPTH_COMPONENT32F, m_spotSize,
                     m_spotSize, (GLsizei)layers);
  glTextureParameteri(m_spotShadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(m_spotShadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(m_spotShadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTextureParameteri(m_spotShadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTextureParameterfv(m_spotShadowTex, GL_TEXTURE_BORDER_COLOR, border);
  m_spotLayers = layers;
}

void ShadowSystem::ensurePointResources(uint32_t layers) {
  if (layers == 0)
    layers = 1;
  if (m_pointShadowTex && m_pointLayers == layers)
    return;
  if (m_pointShadowTex)
    glDeleteTextures(1, &m_pointShadowTex);

  glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &m_pointShadowTex);
  glTextureStorage3D(m_pointShadowTex, 1, GL_R32F, m_pointSize, m_pointSize,
                     (GLsizei)(layers * 6u));
  glTextureParameteri(m_pointShadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTextureParameteri(m_pointShadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTextureParameteri(m_pointShadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTextureParameteri(m_pointShadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTextureParameteri(m_pointShadowTex, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  m_pointLayers = layers;

  glNamedRenderbufferStorage(m_pointDepthRbo, GL_DEPTH_COMPONENT24, m_pointSize,
                             m_pointSize);
}

uint32_t ShadowSystem::shadowIndex(EntityID e, LightType type) const {
  switch (type) {
  case LightType::Directional: {
    auto it = m_dirIndex.find(e);
    return (it == m_dirIndex.end()) ? 0xFFFFFFFFu : it->second;
  }
  case LightType::Spot: {
    auto it = m_spotIndex.find(e);
    return (it == m_spotIndex.end()) ? 0xFFFFFFFFu : it->second;
  }
  case LightType::Point: {
    auto it = m_pointIndex.find(e);
    return (it == m_pointIndex.end()) ? 0xFFFFFFFFu : it->second;
  }
  }
  return 0xFFFFFFFFu;
}

void ShadowSystem::render(const EngineContext &engine,
                          const RenderableRegistry &registry,
                          const RenderPassContext &ctx,
                          const DrawFn &draw) {
  if (!m_dirProg || !m_pointProg)
    return;

  const World &world = engine.world();

  std::vector<EntityID> dirLights;
  std::vector<EntityID> spotLights;
  std::vector<EntityID> pointLights;

  for (EntityID e : world.alive()) {
    if (!world.isAlive(e) || !world.hasLight(e))
      continue;
    const auto &L = world.light(e);
    if (!L.enabled)
      continue;
    switch (L.type) {
    case LightType::Directional:
      dirLights.push_back(e);
      break;
    case LightType::Spot:
      spotLights.push_back(e);
      break;
    case LightType::Point:
      pointLights.push_back(e);
      break;
    }
  }

  m_dirCount = (uint32_t)dirLights.size();
  m_spotCount = (uint32_t)spotLights.size();
  m_pointCount = (uint32_t)pointLights.size();

  ensureDirResources(m_dirCount * 4u);
  ensureSpotResources(m_spotCount);
  ensurePointResources(m_pointCount);

  m_dirIndex.clear();
  m_spotIndex.clear();
  m_pointIndex.clear();
  for (uint32_t i = 0; i < m_dirCount; ++i)
    m_dirIndex[dirLights[i]] = i;
  for (uint32_t i = 0; i < m_spotCount; ++i)
    m_spotIndex[spotLights[i]] = i;
  for (uint32_t i = 0; i < m_pointCount; ++i)
    m_pointIndex[pointLights[i]] = i;

  renderDirectional(engine, registry, dirLights, draw);
  renderSpot(engine, registry, spotLights, draw);
  renderPoint(engine, registry, pointLights, draw);
}

void ShadowSystem::renderDirectional(const EngineContext &engine,
                                     const RenderableRegistry &registry,
                                     const std::vector<EntityID> &lights,
                                     const DrawFn &draw) {
  if (lights.empty())
    return;

  const World &world = engine.world();
  CSMSettings s = m_csmSettings;
  s.nearPlane = engine.cachedCameraNear();
  s.farPlane = engine.cachedCameraFar();
  s.mapSize = m_dirSize;

  std::vector<glm::mat4> matrices;
  matrices.resize(lights.size() * 4u);

  glUseProgram(m_dirProg);
  const int locLVP = glGetUniformLocation(m_dirProg, "u_LightViewProj");
  const int locM = glGetUniformLocation(m_dirProg, "u_Model");

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(s.polyOffsetFactor, s.polyOffsetUnits);

  for (uint32_t li = 0; li < (uint32_t)lights.size(); ++li) {
    const EntityID e = lights[li];
    const glm::mat4 W = world.worldTransform(e).world;
    const glm::vec3 dir = lightDirFromWorld(W);

    const CSMResult csm = buildCSM(
        s, engine.cachedCameraView(), engine.cachedCameraProj(), dir);
    m_csmSplits = csm.splitFar;

    for (uint32_t ci = 0; ci < 4; ++ci) {
      matrices[li * 4u + ci] = csm.slices[ci].lightViewProj;

      const uint32_t layer = li * 4u + ci;
      glNamedFramebufferTextureLayer(m_dirFbo, GL_DEPTH_ATTACHMENT,
                                     m_dirShadowTex, 0, (int)layer);
      glNamedFramebufferDrawBuffer(m_dirFbo, GL_NONE);
      glNamedFramebufferReadBuffer(m_dirFbo, GL_NONE);
      NYX_ASSERT(glCheckNamedFramebufferStatus(m_dirFbo, GL_FRAMEBUFFER) ==
                     GL_FRAMEBUFFER_COMPLETE,
                 "Shadow dir framebuffer incomplete");

      glBindFramebuffer(GL_FRAMEBUFFER, m_dirFbo);
      glViewport(0, 0, (int)m_dirSize, (int)m_dirSize);

      const float one[1] = {1.0f};
      glClearBufferfv(GL_DEPTH, 0, one);

      glUniformMatrix4fv(locLVP, 1, GL_FALSE,
                         &csm.slices[ci].lightViewProj[0][0]);

      for (const auto &r : registry.all()) {
        if (engine.isEntityHidden(r.entity))
          continue;
        if (r.isCamera || r.isLight)
          continue;
        glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
        draw(r.mesh);
      }
    }
  }

  glDisable(GL_POLYGON_OFFSET_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glNamedBufferData(m_dirMatricesSSBO,
                    (GLsizeiptr)(matrices.size() * sizeof(glm::mat4)),
                    matrices.data(), GL_DYNAMIC_DRAW);
}

void ShadowSystem::renderSpot(const EngineContext &engine,
                              const RenderableRegistry &registry,
                              const std::vector<EntityID> &lights,
                              const DrawFn &draw) {
  if (lights.empty())
    return;

  const World &world = engine.world();
  std::vector<glm::mat4> matrices;
  matrices.resize(lights.size());

  glUseProgram(m_dirProg);
  const int locLVP = glGetUniformLocation(m_dirProg, "u_LightViewProj");
  const int locM = glGetUniformLocation(m_dirProg, "u_Model");

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.0f, 4.0f);

  for (uint32_t li = 0; li < (uint32_t)lights.size(); ++li) {
    const EntityID e = lights[li];
    const auto &L = world.light(e);
    const glm::mat4 W = world.worldTransform(e).world;
    const float farZ = std::max(L.radius, 0.1f);
    const glm::mat4 VP = spotViewProj(W, L.outerAngle, 0.1f, farZ);
    matrices[li] = VP;

    glNamedFramebufferTextureLayer(m_spotFbo, GL_DEPTH_ATTACHMENT,
                                   m_spotShadowTex, 0, (int)li);
    glNamedFramebufferDrawBuffer(m_spotFbo, GL_NONE);
    glNamedFramebufferReadBuffer(m_spotFbo, GL_NONE);
    NYX_ASSERT(glCheckNamedFramebufferStatus(m_spotFbo, GL_FRAMEBUFFER) ==
                   GL_FRAMEBUFFER_COMPLETE,
               "Shadow spot framebuffer incomplete");

    glBindFramebuffer(GL_FRAMEBUFFER, m_spotFbo);
    glViewport(0, 0, (int)m_spotSize, (int)m_spotSize);
    const float one[1] = {1.0f};
    glClearBufferfv(GL_DEPTH, 0, one);

    glUniformMatrix4fv(locLVP, 1, GL_FALSE, &VP[0][0]);

    for (const auto &r : registry.all()) {
      if (engine.isEntityHidden(r.entity))
        continue;
      if (r.isCamera || r.isLight)
        continue;
      glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
      draw(r.mesh);
    }
  }

  glDisable(GL_POLYGON_OFFSET_FILL);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glNamedBufferData(m_spotMatricesSSBO,
                    (GLsizeiptr)(matrices.size() * sizeof(glm::mat4)),
                    matrices.data(), GL_DYNAMIC_DRAW);
}

void ShadowSystem::renderPoint(const EngineContext &engine,
                               const RenderableRegistry &registry,
                               const std::vector<EntityID> &lights,
                               const DrawFn &draw) {
  if (lights.empty())
    return;

  const World &world = engine.world();

  glUseProgram(m_pointProg);
  const int locVP = glGetUniformLocation(m_pointProg, "u_ViewProj");
  const int locM = glGetUniformLocation(m_pointProg, "u_Model");
  const int locPos = glGetUniformLocation(m_pointProg, "u_LightPos");
  const int locFar = glGetUniformLocation(m_pointProg, "u_Far");

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);

  glBindFramebuffer(GL_FRAMEBUFFER, m_pointFbo);
  glNamedFramebufferRenderbuffer(m_pointFbo, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, m_pointDepthRbo);

  const glm::mat4 proj =
      glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 1.0f);

  for (uint32_t li = 0; li < (uint32_t)lights.size(); ++li) {
    const EntityID e = lights[li];
    const auto &L = world.light(e);
    const glm::mat4 W = world.worldTransform(e).world;
    const glm::vec3 pos = glm::vec3(W[3]);
    const float farZ = std::max(L.radius, 0.1f);

    glUniform3fv(locPos, 1, &pos[0]);
    glUniform1f(locFar, farZ);

    for (int face = 0; face < 6; ++face) {
      const uint32_t layer = li * 6u + (uint32_t)face;
      glNamedFramebufferTextureLayer(m_pointFbo, GL_COLOR_ATTACHMENT0,
                                     m_pointShadowTex, 0, (int)layer);
      const std::array<GLenum, 1> bufs{GL_COLOR_ATTACHMENT0};
      glNamedFramebufferDrawBuffers(m_pointFbo, 1, bufs.data());

      NYX_ASSERT(glCheckNamedFramebufferStatus(m_pointFbo, GL_FRAMEBUFFER) ==
                     GL_FRAMEBUFFER_COMPLETE,
                 "Shadow point framebuffer incomplete");

      glViewport(0, 0, (int)m_pointSize, (int)m_pointSize);
      const float zero[1] = {1.0f};
      glClearBufferfv(GL_COLOR, 0, zero);
      glClearBufferfv(GL_DEPTH, 0, zero);

      glm::mat4 view;
      cubeFaceView(pos, face, view);
      glm::mat4 vp = proj * view;
      glUniformMatrix4fv(locVP, 1, GL_FALSE, &vp[0][0]);

      for (const auto &r : registry.all()) {
        if (engine.isEntityHidden(r.entity))
          continue;
        if (r.isCamera || r.isLight)
          continue;
        glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
        draw(r.mesh);
      }
    }
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace Nyx
