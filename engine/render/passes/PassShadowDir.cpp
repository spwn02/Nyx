#include "render/passes/PassShadowDir.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "scene/World.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

void PassShadowDir::configure(uint32_t fbo, uint32_t shadowProg,
                              std::function<void(ProcMeshType)> drawFn) {
  m_fbo = fbo;
  m_shadowProg = shadowProg;
  m_draw = std::move(drawFn);
}

static glm::mat4 makeDirLightViewProj(const glm::vec3 &dirW,
                                      float orthoHalf, float nearZ,
                                      float farZ) {
  glm::vec3 D = glm::normalize(dirW);
  glm::vec3 target(0.0f);
  glm::vec3 pos = target - D * (orthoHalf * 2.0f);

  glm::vec3 up(0, 1, 0);
  if (glm::abs(glm::dot(up, D)) > 0.95f)
    up = glm::vec3(0, 0, 1);

  glm::mat4 V = glm::lookAt(pos, target, up);
  glm::mat4 P =
      glm::ortho(-orthoHalf, orthoHalf, -orthoHalf, orthoHalf, nearZ, farZ);
  return P * V;
}

void PassShadowDir::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "ShadowDirDepth",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Shadow.DirDepth", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        RGTextureRef shadowRef = bb.getTexture("Shadow.DirDepth");
        const auto &shadowT = rg.tex(bb.textureHandle(shadowRef));
        const auto &shadowDesc = rg.desc(bb.textureHandle(shadowRef));

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, shadowT.tex, 0);
        const GLenum none = GL_NONE;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &none);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "ShadowDirDepth framebuffer incomplete");

        glViewport(0, 0, (int)shadowDesc.w, (int)shadowDesc.h);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const float clearZ[1] = {1.0f};
        glClearBufferfv(GL_DEPTH, 0, clearZ);

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);

        glUseProgram(m_shadowProg);

        const World &world = engine.world();
        glm::vec3 dirW = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.25f));
        for (EntityID e : world.alive()) {
          if (!world.isAlive(e) || !world.hasLight(e))
            continue;
          const auto &L = world.light(e);
          if (!L.enabled || L.type != LightType::Directional)
            continue;
          const glm::mat4 W = world.worldTransform(e).world;
          dirW = glm::normalize(-glm::vec3(W[2]));
          break;
        }

        const glm::mat4 lightVP =
            makeDirLightViewProj(dirW, m_orthoHalfSize, m_nearZ, m_farZ);
        engine.setShadowDirViewProj(lightVP);

        const int locLVP = glGetUniformLocation(m_shadowProg, "u_LightViewProj");
        const int locM = glGetUniformLocation(m_shadowProg, "u_Model");
        glUniformMatrix4fv(locLVP, 1, GL_FALSE, &lightVP[0][0]);

        for (const auto &r : registry.all()) {
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera)
            continue;
          if (r.isLight)
            continue;
          glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
          if (m_draw)
            m_draw(r.mesh);
        }

        glDisable(GL_POLYGON_OFFSET_FILL);
      });
}

} // namespace Nyx
