#include "PassLightGridDebug.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/gl/GLShaderUtil.h"
#include "render/ViewMode.h"

#include <glad/glad.h>

namespace Nyx {

PassLightGridDebug::~PassLightGridDebug() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassLightGridDebug::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("passes/lightgrid_debug.comp");
  NYX_ASSERT(m_prog != 0, "PassLightGridDebug: shader build failed");
}

void PassLightGridDebug::setup(RenderGraph &graph, const RenderPassContext &ctx,
                               const RenderableRegistry &registry,
                               EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "LightGridDebug",
      [&](RenderPassBuilder &b) {
        b.readBuffer("LightGrid.Meta", RenderAccess::UBORead);
        b.readBuffer("LightGrid.Header", RenderAccess::SSBORead);
        b.writeTexture("HDR.Debug", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        if (engine.viewMode() != ViewMode::LightGrid)
          return;

        NYX_ASSERT(m_prog != 0, "PassLightGridDebug: not initialized");
        const auto &meta = buf(bb, rg, "LightGrid.Meta");
        const auto &header = buf(bb, rg, "LightGrid.Header");
        const auto &out = tex(bb, rg, "HDR.Debug");

        NYX_ASSERT(meta.buf && header.buf && out.tex,
                   "PassLightGridDebug: missing resources");

        glUseProgram(m_prog);
        glBindBufferBase(GL_UNIFORM_BUFFER, 22, meta.buf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 24, header.buf);
        glBindImageTexture(1, out.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);

        const GLint locOut = glGetUniformLocation(m_prog, "uOutSize");
        if (locOut >= 0)
          glUniform2ui(locOut, rc.fbWidth, rc.fbHeight);

        const uint32_t gx = (rc.fbWidth + 15u) / 16u;
        const uint32_t gy = (rc.fbHeight + 15u) / 16u;
        glDispatchCompute(gx, gy, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
