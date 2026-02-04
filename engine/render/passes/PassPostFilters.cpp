#include "PassPostFilters.h"

#include "core/Assert.h"
#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLShaderUtil.h"

#include <glad/glad.h>

namespace Nyx {

PassPostFilters::~PassPostFilters() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }

  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
}

void PassPostFilters::configure(GLShaderUtil &shaders, GLResources &res,
                                GLFullscreenTriangle &fsTri) {
  m_prog = shaders.buildProgramVF("fullscreen.vert", "present.frag");
  m_fbo = res.acquireFBO();
  m_fsTri = &fsTri;
}

void PassPostFilters::setup(RenderGraph &graph, const RenderPassContext &ctx,
                            const RenderableRegistry &registry,
                            EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "PostFilters",
      [&](RenderPassBuilder &b) {
        b.readTexture("Post.In", RenderAccess::SampledRead);
        b.writeTexture("LDR.Color", RenderAccess::ColorWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &postT = tex(bb, rg, "Post.In");
        const auto &ldrT = tex(bb, rg, "LDR.Color");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, ldrT.tex, 0);
        const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuf);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "PostFilters framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(m_prog);
        if (m_fsTri)
          glBindVertexArray(m_fsTri->vao);

        glBindTextureUnit(0, postT.tex);
        glDrawArrays(GL_TRIANGLES, 0, 3);
      });
}

} // namespace Nyx
