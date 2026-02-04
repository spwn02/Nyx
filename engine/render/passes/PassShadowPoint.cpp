#include "PassShadowPoint.h"
#include "../../scene/World.h"
#include "../../scene/Components.h"
#include "app/EngineContext.h"
#include "scene/RenderableRegistry.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

PassShadowPoint::~PassShadowPoint() {
  if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
}

void PassShadowPoint::configure(GLShaderUtil &shaders, GLResources &res,
                                std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;
  m_draw = drawFn;
  m_prog = shaders.buildProgramVF("shadow_point.vert", "shadow_point.frag");
  glCreateFramebuffers(1, &m_fbo);
}

void PassShadowPoint::setup(RenderGraph &graph, const RenderPassContext &ctx,
                            const RenderableRegistry &registry,
                            EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;
  (void)registry;

  graph.addPass(
      "ShadowPoint",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Shadow.PointArray", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        if (m_prog == 0) return;

        const auto &atlas = tex(bb, rg, "Shadow.PointArray");
        if (!atlas.tex) return;

        // Query all point lights that cast shadows
        m_pointLights.clear();
        uint32_t arrayIndex = 0;

        for (EntityID e : engine.world().alive()) {
          if (!engine.world().isAlive(e) || !engine.world().hasLight(e))
            continue;
          
          const auto &L = engine.world().light(e);
          if (L.type != LightType::Point || !L.enabled || !L.castShadow)
            continue;

          if (arrayIndex >= m_maxPointLights)
            break; // Reached maximum point lights

          const glm::vec3 pos = engine.world().worldPosition(e);
          const float farPlane = std::max(L.pointFar, 1.0f);
          const float nearPlane = 0.1f;

          // Build 6 face projection matrices for cubemap
          glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
          
          glm::mat4 views[6] = {
            glm::lookAt(pos, pos + glm::vec3( 1, 0, 0), glm::vec3(0,-1, 0)), // +X
            glm::lookAt(pos, pos + glm::vec3(-1, 0, 0), glm::vec3(0,-1, 0)), // -X
            glm::lookAt(pos, pos + glm::vec3( 0, 1, 0), glm::vec3(0, 0, 1)), // +Y
            glm::lookAt(pos, pos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)), // -Y
            glm::lookAt(pos, pos + glm::vec3( 0, 0, 1), glm::vec3(0,-1, 0)), // +Z
            glm::lookAt(pos, pos + glm::vec3( 0, 0,-1), glm::vec3(0,-1, 0))  // -Z
          };

          PointLightShadow pls;
          pls.entity = e;
          pls.arrayIndex = arrayIndex;
          pls.position = pos;
          pls.farPlane = farPlane;
          for (int i = 0; i < 6; ++i) {
            pls.viewProj[i] = proj * views[i];
          }

          m_pointLights.push_back(pls);
          arrayIndex++;
        }

        if (m_pointLights.empty()) return;

        glUseProgram(m_prog);
        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        const int locLightPos = glGetUniformLocation(m_prog, "uLightPos");
        const int locFarPlane = glGetUniformLocation(m_prog, "uFarPlane");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
        glNamedFramebufferReadBuffer(m_fbo, GL_NONE);
        glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT, atlas.tex, 0,
                                       0);
        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PassShadowPoint: FBO incomplete");

        glViewport(0, 0, m_cubemapResolution, m_cubemapResolution);
        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);

        // For each point light, render all 6 faces into 2D array layers
        for (const auto &pl : m_pointLights) {
          glUniform3fv(locLightPos, 1, &pl.position[0]);
          glUniform1f(locFarPlane, pl.farPlane);

          for (int face = 0; face < 6; ++face) {
            const uint32_t layer = pl.arrayIndex * 6u + (uint32_t)face;
            glNamedFramebufferTextureLayer(m_fbo, GL_DEPTH_ATTACHMENT, atlas.tex,
                                           0, (GLint)layer);
            glClear(GL_DEPTH_BUFFER_BIT);
            glUniformMatrix4fv(locVP, 1, GL_FALSE, &pl.viewProj[face][0][0]);

            // Render scene geometry
            for (EntityID e : engine.world().alive()) {
              if (!engine.world().isAlive(e) || !engine.world().hasMesh(e))
                continue;

              const auto &mesh = engine.world().mesh(e);
              const glm::mat4 model = engine.world().worldTransform(e).world;
              glUniformMatrix4fv(locM, 1, GL_FALSE, &model[0][0]);

              for (const auto &submesh : mesh.submeshes) {
                m_draw(submesh.type);
              }
            }
          }
        }
      });
}

} // namespace Nyx
