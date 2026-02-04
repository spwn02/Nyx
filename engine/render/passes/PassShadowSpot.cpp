#include "PassShadowSpot.h"
#include "../../scene/World.h"
#include "../../scene/Components.h"
#include "../../core/Log.h"
#include "app/EngineContext.h"
#include "scene/RenderableRegistry.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

PassShadowSpot::~PassShadowSpot() {
  if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
}

void PassShadowSpot::configure(GLShaderUtil &shaders, GLResources &res,
                               std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;
  m_draw = drawFn;
  m_prog = shaders.buildProgramVF("shadow_spot.vert", "shadow_spot.frag");
  glCreateFramebuffers(1, &m_fbo);
  m_atlasAlloc.reset(m_atlasW, m_atlasH);
}

void PassShadowSpot::setup(RenderGraph &graph, const RenderPassContext &ctx,
                           const RenderableRegistry &registry,
                           EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;
  (void)registry;

  graph.addPass(
      "ShadowSpot",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Shadow.SpotAtlas", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        if (m_prog == 0) return;

        const auto &atlas = tex(bb, rg, "Shadow.SpotAtlas");
        if (!atlas.tex) return;

        // Query all spot lights that cast shadows
        m_spotLights.clear();
        std::vector<uint64_t> aliveKeys;

        uint32_t lightIdx = 0;
        for (EntityID e : engine.world().alive()) {
          if (!engine.world().isAlive(e) || !engine.world().hasLight(e))
            continue;
          
          const auto &L = engine.world().light(e);
          if (L.type != LightType::Spot || !L.enabled || !L.castShadow)
            continue;

          const glm::vec3 pos = engine.world().worldPosition(e);
          const glm::vec3 dir = engine.world().worldDirection(e, glm::vec3(0, 0, -1));
          
          const uint64_t key = (uint64_t(e.index) << 32) | uint64_t(e.generation);
          aliveKeys.push_back(key);
          
          const uint16_t shadowRes = std::max(uint16_t(256), L.shadowRes);
          ShadowTile tile = m_atlasAlloc.acquire(key, shadowRes, 4);

          // Build spot light projection
          const float fov = std::max(L.outerAngle * 2.0f, 0.1f);
          const float aspect = 1.0f;
          const float nearPlane = 0.1f;
          const float farPlane = std::max(L.radius, 1.0f);
          
          glm::mat4 proj = glm::perspective(fov, aspect, nearPlane, farPlane);
          glm::mat4 view = glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));
          glm::mat4 vp = proj * view;

          m_spotLights.push_back({e, tile, vp, pos, dir, L.innerAngle, L.outerAngle});
          lightIdx++;
        }

        m_atlasAlloc.endFrameAndRecycleUnused(aliveKeys);

        if (m_spotLights.empty()) return;

        glUseProgram(m_prog);
        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, atlas.tex, 0);
        glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
        glNamedFramebufferReadBuffer(m_fbo, GL_NONE);
        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PassShadowSpot: FBO incomplete");

        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);

        // Render each spot light to its tile
        for (const auto &spot : m_spotLights) {
          const auto &tile = spot.tile;
          
          glViewport(tile.ix(), tile.iy(), tile.iw(), tile.ih());
          glScissor(tile.ix(), tile.iy(), tile.iw(), tile.ih());
          glEnable(GL_SCISSOR_TEST);
          glClear(GL_DEPTH_BUFFER_BIT);

          glUniformMatrix4fv(locVP, 1, GL_FALSE, &spot.viewProj[0][0]);

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

        glDisable(GL_SCISSOR_TEST);
      });
}

} // namespace Nyx
