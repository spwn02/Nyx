#include "PassTransparentOIT.h"

#include "app/EngineContext.h"
#include "core/Assert.h"

#include <glad/glad.h>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kPerDrawBinding = 13;

PassTransparentOIT::~PassTransparentOIT() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
}

void PassTransparentOIT::configure(GLShaderUtil &shaders, GLResources &res) {
  m_res = &res;
  m_fbo = res.acquireFBO();
  m_prog = shaders.buildProgramVF("transparent_oit.vert",
                                  "transparent_oit.frag");
}

void PassTransparentOIT::setup(RenderGraph &graph, const RenderPassContext &ctx,
                               const RenderableRegistry &registry,
                               EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "TransparentOIT",
      [&](RenderPassBuilder &b) {
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.writeTexture("Depth.Pre", RenderAccess::DepthWrite);
        b.writeTexture("Trans.Accum", RenderAccess::ColorWrite);
        b.writeTexture("Trans.Reveal", RenderAccess::ColorWrite);
        b.readBuffer("Scene.Lights", RenderAccess::SSBORead);
        b.readBuffer("Scene.PerDraw", RenderAccess::SSBORead);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &depT = tex(bb, rg, "Depth.Pre");
        const auto &accT = tex(bb, rg, "Trans.Accum");
        const auto &revT = tex(bb, rg, "Trans.Reveal");

        NYX_ASSERT(depT.tex && accT.tex && revT.tex,
                   "TransparentOIT: missing textures");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, accT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT1, revT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const GLenum bufs[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glNamedFramebufferDrawBuffers(m_fbo, 2, bufs);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "TransparentOIT framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);

        const float clear0[4] = {0, 0, 0, 0};
        const float clear1[4] = {1, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, clear0);
        glClearBufferfv(GL_COLOR, 1, clear1);

        glEnable(GL_BLEND);
        glBlendEquationi(0, GL_FUNC_ADD);
        glBlendFunci(0, GL_ONE, GL_ONE);
        glBlendEquationi(1, GL_FUNC_ADD);
        glBlendFunci(1, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);

        glUseProgram(m_prog);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kPerDrawBinding,
                         engine.perDraw().ssbo());

        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);

        const auto &drawList = registry.transparentSorted();
        const uint32_t baseOffset = engine.perDrawTransparentOffset();
        uint32_t visibleIdx = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); ++i) {
          const auto &r = drawList[i];
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera)
            continue;
          engine.rendererDrawPrimitive(static_cast<uint32_t>(r.mesh),
                                       baseOffset + visibleIdx);
          visibleIdx++;
        }

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
      });
}

} // namespace Nyx
