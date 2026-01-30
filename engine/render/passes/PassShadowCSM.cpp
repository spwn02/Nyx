#include "render/passes/PassShadowCSM.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "scene/World.h"

#include <glad/glad.h>

namespace Nyx {

void PassShadowCSM::configure(uint32_t fbo, uint32_t shadowProg,
                              std::function<void(ProcMeshType)> drawFn) {
  m_fbo = fbo;
  m_shadowProg = shadowProg;
  m_draw = std::move(drawFn);
}

static const char *csmName(int i) {
  switch (i) {
  case 0:
    return "Shadow.CSM0";
  case 1:
    return "Shadow.CSM1";
  case 2:
    return "Shadow.CSM2";
  default:
    return "Shadow.CSM3";
  }
}

void PassShadowCSM::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "ShadowCSMDepth",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Shadow.CSM0", RenderAccess::DepthWrite);
        b.writeTexture("Shadow.CSM1", RenderAccess::DepthWrite);
        b.writeTexture("Shadow.CSM2", RenderAccess::DepthWrite);
        b.writeTexture("Shadow.CSM3", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const World &world = engine.world();

        glm::vec3 lightDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.25f));
        for (EntityID e : world.alive()) {
          if (!world.isAlive(e) || !world.hasLight(e))
            continue;
          const auto &L = world.light(e);
          if (!L.enabled || L.type != LightType::Directional)
            continue;
          const glm::mat4 W = world.worldTransform(e).world;
          lightDir = glm::normalize(-glm::vec3(W[2]));
          break;
        }

        CSMSettings s = m_settings;
        s.nearPlane = engine.cachedCameraNear();
        s.farPlane = engine.cachedCameraFar();

        const CSMResult csm =
            buildCSM(s, engine.cachedCameraView(), engine.cachedCameraProj(),
                     lightDir);
        engine.setCachedCSM(csm);

        glUseProgram(m_shadowProg);
        const int locLVP = glGetUniformLocation(m_shadowProg, "u_LightViewProj");
        const int locM = glGetUniformLocation(m_shadowProg, "u_Model");

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(s.polyOffsetFactor, s.polyOffsetUnits);

        for (int ci = 0; ci < 4; ++ci) {
          RGTextureRef ref = bb.getTexture(csmName(ci));
          const auto &tex = rg.tex(bb.textureHandle(ref));
          const auto &desc = rg.desc(bb.textureHandle(ref));

          glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
          glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, tex.tex, 0);
          const GLenum none = GL_NONE;
          glNamedFramebufferDrawBuffers(m_fbo, 1, &none);

          NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                         GL_FRAMEBUFFER_COMPLETE,
                     "ShadowCSMDepth framebuffer incomplete");

          glViewport(0, 0, (int)desc.w, (int)desc.h);
          const float clearZ[1] = {1.0f};
          glClearBufferfv(GL_DEPTH, 0, clearZ);

          glUniformMatrix4fv(locLVP, 1, GL_FALSE,
                             &csm.slices[ci].lightViewProj[0][0]);

          for (const auto &r : registry.all()) {
            if (engine.isEntityHidden(r.entity))
              continue;
            if (r.isCamera || r.isLight)
              continue;
            glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
            if (m_draw)
              m_draw(r.mesh);
          }
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
      });
}

} // namespace Nyx
