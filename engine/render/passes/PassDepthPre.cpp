#include "PassDepthPre.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "scene/RenderableRegistry.h"

#include <glad/glad.h>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kLightsBinding = 16;

PassDepthPre::~PassDepthPre() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassDepthPre::configure(GLShaderUtil &shader, GLResources &res,
                             std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;

  m_fbo = res.acquireFBO();
  m_prog = shader.buildProgramVF("forward.vert", "forward.frag");
  m_draw = std::move(drawFn);
}

void PassDepthPre::setup(RenderGraph &graph, const RenderPassContext &ctx,
                         const RenderableRegistry &registry,
                         EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "DepthPre",
      [&](RenderPassBuilder &b) {
        b.writeTexture("Depth.Pre", RenderAccess::DepthWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &depT = tex(bb, rg, "Depth.Pre");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const GLenum none = GL_NONE;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &none);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "DepthPre framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);

        const float clearZ[1] = {1.0f};
        glClearBufferfv(GL_DEPTH, 0, clearZ);

        engine.materials().uploadIfDirty();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightsBinding,
                         engine.lights().ssbo());

        glUseProgram(m_prog);

        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        const int locV = glGetUniformLocation(m_prog, "u_View");
        const int locM = glGetUniformLocation(m_prog, "u_Model");
        const int locPick = glGetUniformLocation(m_prog, "u_PickID");
        const int locVM = glGetUniformLocation(m_prog, "u_ViewMode");
        const int locMat = glGetUniformLocation(m_prog, "u_MaterialIndex");
        const int locIsLight = glGetUniformLocation(m_prog, "u_IsLight");
        const int locLightColorInt =
            glGetUniformLocation(m_prog, "u_LightColorIntensity");
        const int locLightExposure =
            glGetUniformLocation(m_prog, "u_LightExposure");

        glUniform1ui(locVM, static_cast<uint32_t>(engine.viewMode()));
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        if (locV >= 0)
          glUniformMatrix4fv(locV, 1, GL_FALSE, &rc.view[0][0]);

        for (const auto &r : registry.all()) {
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera)
            continue;
          glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
          glUniform1ui(locPick, r.pickID);
          glUniform1ui(locMat, engine.materialIndex(r));
          if (locIsLight >= 0)
            glUniform1i(locIsLight, r.isLight ? 1 : 0);
          if (r.isLight) {
            if (locLightColorInt >= 0) {
              glUniform4f(locLightColorInt, r.lightColor.x, r.lightColor.y,
                          r.lightColor.z, r.lightIntensity);
            }
            if (locLightExposure >= 0)
              glUniform1f(locLightExposure, r.lightExposure);
          }
          if (m_draw)
            m_draw(r.mesh);
        }
      });
}

} // namespace Nyx
