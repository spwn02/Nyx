#include "PassPresent.h"

#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLShaderUtil.h"

#include <glad/glad.h>

namespace Nyx {

PassPresent::~PassPresent() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassPresent::configure(GLShaderUtil &shaders,
                            GLFullscreenTriangle &fsTri) {
  m_prog = shaders.buildProgramVF("present.vert", "present.frag");
  m_fsTri = &fsTri;
}

void PassPresent::setup(RenderGraph &graph, const RenderPassContext &ctx,
                        const RenderableRegistry &registry,
                        EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;

  graph.addPass(
      "Present",
      [&](RenderPassBuilder &b) {
        b.readTexture("OUT.Color", RenderAccess::SampledRead);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        if (editorVisible)
          return;

        const auto &outT = tex(bb, rg, "OUT.Color");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(m_prog);
        if (m_fsTri)
          glBindVertexArray(m_fsTri->vao);

        glBindTextureUnit(0, outT.tex);

        glDrawArrays(GL_TRIANGLES, 0, 3);
      });
}

} // namespace Nyx
