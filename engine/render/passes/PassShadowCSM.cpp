#include "PassShadowCSM.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "scene/RenderableRegistry.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <algorithm>
#include <cmath>

namespace Nyx {

namespace {

void computeSplitDepths(float n, float f, float lambda, uint32_t cascadeCount,
                        float outSplits[4]) {
  cascadeCount = std::max(1u, std::min(4u, cascadeCount));
  lambda = std::clamp(lambda, 0.0f, 1.0f);

  const float range = f - n;
  const float ratio = f / n;

  for (uint32_t i = 1; i <= cascadeCount; ++i) {
    const float p = float(i) / float(cascadeCount);
    const float logSplit = n * std::pow(ratio, p);
    const float uniSplit = n + range * p;
    const float split = uniSplit * (1.0f - lambda) + logSplit * lambda;
    outSplits[i - 1] = split;
  }
  for (uint32_t i = cascadeCount; i < 4; ++i)
    outSplits[i] = f;
}

glm::vec3 dirFromYawPitchDeg(float yawDeg, float pitchDeg) {
  const float y = glm::radians(yawDeg);
  const float p = glm::radians(pitchDeg);
  glm::vec3 d{std::cos(y) * std::cos(p), std::sin(p),
              std::sin(y) * std::cos(p)};
  return glm::normalize(d);
}

static std::array<glm::vec3, 8>
frustumSliceCornersWS(const glm::mat4 &camView, const glm::mat4 &camProj,
                      float nearDist, float farDist) {
  const float f = camProj[1][1];
  const float aspect = camProj[1][1] / camProj[0][0];
  const float tanHalfFovy = 1.0f / f;

  const float nh = nearDist * tanHalfFovy;
  const float nw = nh * aspect;
  const float fh = farDist * tanHalfFovy;
  const float fw = fh * aspect;

  const glm::mat4 invV = glm::inverse(camView);
  const glm::vec3 camRight = glm::normalize(glm::vec3(invV[0]));
  const glm::vec3 camUp = glm::normalize(glm::vec3(invV[1]));
  const glm::vec3 camFwd = -glm::normalize(glm::vec3(invV[2]));
  const glm::vec3 camPos = glm::vec3(invV[3]);

  const glm::vec3 nc = camPos + camFwd * nearDist;
  const glm::vec3 fc = camPos + camFwd * farDist;

  const glm::vec3 ntl = nc + camUp * nh - camRight * nw;
  const glm::vec3 ntr = nc + camUp * nh + camRight * nw;
  const glm::vec3 nbl = nc - camUp * nh - camRight * nw;
  const glm::vec3 nbr = nc - camUp * nh + camRight * nw;

  const glm::vec3 ftl = fc + camUp * fh - camRight * fw;
  const glm::vec3 ftr = fc + camUp * fh + camRight * fw;
  const glm::vec3 fbl = fc - camUp * fh - camRight * fw;
  const glm::vec3 fbr = fc - camUp * fh + camRight * fw;

  return {ntl, ntr, nbl, nbr, ftl, ftr, fbl, fbr};
}

static glm::mat4 makeLightView(const glm::vec3 &centerWS,
                               const glm::vec3 &lightDir,
                               float viewDistance) {
  const glm::vec3 dir = glm::normalize(lightDir);
  const glm::vec3 eye = centerWS - dir * viewDistance;
  glm::vec3 up = (std::abs(dir.y) > 0.95f) ? glm::vec3(0, 0, 1)
                                          : glm::vec3(0, 1, 0);
  return glm::lookAt(eye, centerWS, up);
}

static void aabbFromPointsLS(const glm::mat4 &lightView,
                             const std::array<glm::vec3, 8> &ptsWS,
                             glm::vec3 &outMin, glm::vec3 &outMax) {
  glm::vec3 mn(1e30f);
  glm::vec3 mx(-1e30f);

  for (const auto &p : ptsWS) {
    glm::vec4 ls = lightView * glm::vec4(p, 1.0f);
    mn = glm::min(mn, glm::vec3(ls));
    mx = glm::max(mx, glm::vec3(ls));
  }
  outMin = mn;
  outMax = mx;
}

static void stabilizeOrthoBounds(glm::vec3 &mn, glm::vec3 &mx,
                                 uint32_t shadowRes) {
  const float w = mx.x - mn.x;
  const float h = mx.y - mn.y;

  const float texelSizeX = (w > 1e-6f) ? (w / float(shadowRes)) : 1.0f;
  const float texelSizeY = (h > 1e-6f) ? (h / float(shadowRes)) : 1.0f;

  mn.x = std::floor(mn.x / texelSizeX) * texelSizeX;
  mn.y = std::floor(mn.y / texelSizeY) * texelSizeY;
  mx.x = mn.x + w;
  mx.y = mn.y + h;
}

} // namespace

PassShadowCSM::~PassShadowCSM() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassShadowCSM::configure(GLShaderUtil &shader, GLResources &res,
                              std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;
  m_fbo = res.acquireFBO();
  m_prog = shader.buildProgramVF("passes/shadow_csm.vert",
                                 "passes/shadow_csm.frag");
  m_draw = std::move(drawFn);
  
  // Initialize shadow atlas (4096x4096 for 4 cascades)
  m_atlasAlloc.reset(4096, 4096);
}

void PassShadowCSM::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "ShadowCSM",
      [&](RenderPassBuilder &b) {
        // Use single 4096x4096 atlas for all cascades
        b.writeTexture("Shadow.CSMAtlas", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PassShadowCSM: missing program");

        const auto &atlas = tex(bb, rg, "Shadow.CSMAtlas");
        NYX_ASSERT(atlas.tex, "PassShadowCSM: missing CSM atlas texture");

        const uint32_t cascadeCount = std::max(1u, std::min(4u, m_cfg.cascadeCount));
        const uint32_t shadowRes = std::max(256u, m_cfg.shadowRes);

        // Allocate tiles in atlas for each cascade
        std::vector<uint64_t> aliveKeys;
        for (uint32_t i = 0; i < cascadeCount; ++i) {
          const uint64_t key = i; // Simple key per cascade
          aliveKeys.push_back(key);
          m_cascadeTiles[i] = m_atlasAlloc.acquire(key, shadowRes, 4);
        }
        m_atlasAlloc.endFrameAndRecycleUnused(aliveKeys);

        const float camNear =
            std::max(0.0001f, engine.cachedCameraNear());
        const float camFar =
            std::max(camNear + 0.01f, engine.cachedCameraFar());

        float splits[4];
        computeSplitDepths(camNear, camFar, m_cfg.splitLambda, cascadeCount,
                           splits);


        glm::vec3 lightDir = dirFromYawPitchDeg(m_cfg.lightDirYawDeg,
                                               m_cfg.lightDirPitchDeg);
        if (engine.lights().hasPrimaryDirLight()) {
          const uint64_t key = engine.lights().primaryDirLightKey();
          const EntityID e{(uint32_t)(key >> 32), (uint32_t)(key & 0xFFFFFFFFu)};
          if (engine.world().isAlive(e) && engine.world().hasLight(e)) {
            const glm::mat4 W = engine.world().worldTransform(e).world;
            const glm::vec3 fwd = glm::normalize(-glm::mat3(W) * glm::vec3(0, 0, 1));
            lightDir = fwd;
          }
        } else {
          float bestIntensity = -1.0f;
          for (EntityID e : engine.world().alive()) {
            if (!engine.world().isAlive(e) || !engine.world().hasLight(e))
              continue;
            const auto &L = engine.world().light(e);
            if (L.type != LightType::Directional || !L.enabled)
              continue;
            const float intensity = std::max(0.0f, L.intensity);
            if (intensity <= bestIntensity)
              continue;

            const glm::mat4 W = engine.world().worldTransform(e).world;
            const glm::vec3 fwd = glm::normalize(-glm::mat3(W) * glm::vec3(0, 0, 1));
            lightDir = fwd;
            bestIntensity = intensity;
          }
        }

        m_uboCPU.splitDepths = glm::vec4(splits[0], splits[1], splits[2], splits[3]);
        m_uboCPU.shadowMapSize =
            glm::vec4(float(shadowRes), float(shadowRes),
                      1.0f / float(shadowRes), 1.0f / float(shadowRes));
        m_uboCPU.biasParams = glm::vec4(m_cfg.normalBias, m_cfg.receiverBias,
                                        m_cfg.slopeBias, 0.0f);
        m_uboCPU.misc = glm::vec4(float(cascadeCount), camNear, camFar, 0.0f);
        m_uboCPU.lightDir = glm::vec4(lightDir, 0.0f);

        glUseProgram(m_prog);

        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locVP = glGetUniformLocation(m_prog, "u_LightViewProj");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, atlas.tex, 0);
        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PassShadowCSM: FBO incomplete");

        // Clear entire atlas once
        glViewport(0, 0, 4096, 4096);
        glClearDepth(1.0);
        glClear(GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(m_cfg.rasterSlopeScale, m_cfg.rasterConstant);

        if (m_cfg.cullFrontFaces) {
          glEnable(GL_CULL_FACE);
          glCullFace(GL_FRONT);
        } else {
          glDisable(GL_CULL_FACE);
        }

        glEnable(GL_SCISSOR_TEST);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);

        auto renderCascade = [&](int ci) {
          const ShadowTile &tile = m_cascadeTiles[ci];
          
          // Set viewport and scissor to tile region
          glViewport((int)tile.ix(), (int)tile.iy(), (int)tile.iw(), (int)tile.ih());
          glScissor((int)tile.ix(), (int)tile.iy(), (int)tile.iw(), (int)tile.ih());

          const float nearD = (ci == 0) ? camNear : splits[ci - 1];
          const float farD = splits[ci];
          const auto cornersWS =
              frustumSliceCornersWS(rc.view, rc.proj, nearD, farD);

          glm::vec3 center(0.0f);
          for (const auto &p : cornersWS)
            center += p;
          center /= 8.0f;

          glm::mat4 lightView =
              makeLightView(center, lightDir, m_cfg.lightViewDistance);

          glm::vec3 mnLS, mxLS;
          aabbFromPointsLS(lightView, cornersWS, mnLS, mxLS);

          mnLS.x -= m_cfg.aabbPadding;
          mnLS.y -= m_cfg.aabbPadding;
          mxLS.x += m_cfg.aabbPadding;
          mxLS.y += m_cfg.aabbPadding;

          mnLS.z -= m_cfg.zPadding;
          mxLS.z += m_cfg.zPadding;

          if (m_cfg.stabilize)
            stabilizeOrthoBounds(mnLS, mxLS, shadowRes);

          const float left = mnLS.x;
          const float right = mxLS.x;
          const float bottom = mnLS.y;
          const float top = mxLS.y;
          const float nearPlane = -mxLS.z;
          const float farPlane = -mnLS.z;

          glm::mat4 lightProj =
              glm::ortho(left, right, bottom, top, nearPlane, farPlane);

          const glm::mat4 lightVP = lightProj * lightView;
          m_uboCPU.lightViewProj[ci] = lightVP;
          
          // Store atlas UV coordinates for this cascade tile
          float u0, v0, u1, v1;
          tile.uvClamp(u0, v0, u1, v1);
          m_uboCPU.atlasUVMin[ci] = glm::vec4(u0, v0, 0, 0);
          m_uboCPU.atlasUVMax[ci] = glm::vec4(u1, v1, 0, 0);

          glUniformMatrix4fv(locVP, 1, GL_FALSE, &lightVP[0][0]);

          for (const auto &r : registry.all()) {
            if (engine.isEntityHidden(r.entity))
              continue;
            if (r.isCamera)
              continue;
            if (r.isLight)
              continue;
            if (locM >= 0)
              glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
            if (m_draw)
              m_draw(r.mesh);
          }
        };

        for (uint32_t ci = 0; ci < cascadeCount; ++ci) {
          renderCascade(ci);
        }

        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_CULL_FACE);

        glNamedBufferSubData(engine.shadowCSMUBO(), 0, sizeof(ShadowCSMUBO),
                             &m_uboCPU);
        glBindBufferBase(GL_UNIFORM_BUFFER, 5, engine.shadowCSMUBO());
      });
}

} // namespace Nyx
