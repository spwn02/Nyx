#include "PassShadowDir.h"
#include "../../scene/World.h"
#include "../../scene/Components.h"
#include "app/EngineContext.h"
#include "scene/RenderableRegistry.h"
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Nyx {

PassShadowDir::~PassShadowDir() {
  if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
}

void PassShadowDir::configure(GLShaderUtil &shaders, GLResources &res,
                              std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;
  m_draw = drawFn;
  m_prog = shaders.buildProgramVF("shadow_dir.vert", "shadow_dir.frag");
  glCreateFramebuffers(1, &m_fbo);
  m_atlasAlloc.reset(m_atlasW, m_atlasH);
}

void PassShadowDir::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;
  (void)registry;

  graph.addPass(
      "ShadowDir",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Shadow.DirAtlas", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        if (m_prog == 0) return;

        const auto &atlas = tex(bb, rg, "Shadow.DirAtlas");
        if (!atlas.tex) return;

        // Query all directional lights that cast shadows
        // Skip the primary one (handled by CSM)
        m_dirLights.clear();
        std::vector<uint64_t> aliveKeys;

        struct DirLightCandidate {
          EntityID entity;
          float intensity;
          glm::vec3 direction;
        };
        std::vector<DirLightCandidate> candidates;

        for (EntityID e : engine.world().alive()) {
          if (!engine.world().isAlive(e) || !engine.world().hasLight(e))
            continue;
          const auto &tr = engine.world().transform(e);
          if (tr.hidden || tr.hiddenEditor || tr.disabledAnim)
            continue;
          
          const auto &L = engine.world().light(e);
          if (L.type != LightType::Directional || !L.enabled || !L.castShadow)
            continue;

          const glm::vec3 dir = engine.world().worldDirection(e, glm::vec3(0, 0, -1));
          candidates.push_back({e, L.intensity, dir});
        }

        // Sort by intensity and skip the CSM primary
        std::sort(candidates.begin(), candidates.end(),
                  [](const auto &a, const auto &b) { return a.intensity > b.intensity; });

        const uint64_t primaryKey = engine.lights().hasPrimaryDirLight()
                                        ? engine.lights().primaryDirLightKey()
                                        : 0u;
        for (size_t i = 0; i < candidates.size(); ++i) {
          const auto &cand = candidates[i];
          EntityID e = cand.entity;
          const uint64_t key = (uint64_t(e.index) << 32) | uint64_t(e.generation);
          if (primaryKey != 0u && key == primaryKey)
            continue;
          const auto &L = engine.world().light(e);

          aliveKeys.push_back(key);
          
          const uint16_t shadowRes = std::max(uint16_t(512), L.shadowRes);
          ShadowTile tile = m_atlasAlloc.acquire(key, shadowRes, 4);

          // Build orthographic projection for directional light
          // Cover scene bounds - simplified to fixed size for now
          const float sceneSize = 50.0f;
          const float nearPlane = -100.0f;
          const float farPlane = 100.0f;
          
          glm::mat4 proj = glm::ortho(-sceneSize, sceneSize, -sceneSize, sceneSize,
                                      nearPlane, farPlane);
          
          // View matrix looking down the light direction
          glm::vec3 lightPos = -cand.direction * 50.0f; // Positioned far back
          glm::vec3 up = (std::abs(cand.direction.y) > 0.95f) ? glm::vec3(0, 0, 1)
                                                                : glm::vec3(0, 1, 0);
          glm::mat4 view = glm::lookAt(lightPos, glm::vec3(0), up);
          glm::mat4 vp = proj * view;

          m_dirLights.push_back({e, tile, vp, cand.direction});
        }

        m_atlasAlloc.endFrameAndRecycleUnused(aliveKeys);

        if (m_dirLights.empty()) return;

        glUseProgram(m_prog);
        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, atlas.tex, 0);
        glNamedFramebufferDrawBuffer(m_fbo, GL_NONE);
        glNamedFramebufferReadBuffer(m_fbo, GL_NONE);
        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PassShadowDir: FBO incomplete");

        glClearDepth(1.0);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);

        // Render each directional light to its tile
        for (const auto &dirLight : m_dirLights) {
          const auto &tile = dirLight.tile;
          
          glViewport(tile.ix(), tile.iy(), tile.iw(), tile.ih());
          glScissor(tile.ix(), tile.iy(), tile.iw(), tile.ih());
          glEnable(GL_SCISSOR_TEST);
          glClear(GL_DEPTH_BUFFER_BIT);

          glUniformMatrix4fv(locVP, 1, GL_FALSE, &dirLight.viewProj[0][0]);

          // Render scene geometry
          for (EntityID e : engine.world().alive()) {
            if (!engine.world().isAlive(e) || !engine.world().hasMesh(e))
              continue;
            const auto &tr = engine.world().transform(e);
            if (tr.hidden || tr.hiddenEditor || tr.disabledAnim)
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
