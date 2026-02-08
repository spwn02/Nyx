#include "PassTransparentOITComposite.h"

#include "core/Assert.h"

#include <glad/glad.h>

namespace Nyx {

PassTransparentOITComposite::~PassTransparentOITComposite() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassTransparentOITComposite::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("transparent_oit_composite.comp");
  NYX_ASSERT(m_prog != 0, "TransparentOITComposite: shader build failed");
}

void PassTransparentOITComposite::setup(
    RenderGraph &graph, const RenderPassContext &ctx,
    const RenderableRegistry &registry, EngineContext &engine,
    bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "TransparentOITComposite",
      [&](RenderPassBuilder &b) {
        b.readTexture("HDR.Color", RenderAccess::SampledRead);
        b.readTexture("Trans.Accum", RenderAccess::SampledRead);
        b.readTexture("Trans.Reveal", RenderAccess::SampledRead);
        b.writeTexture("HDR.OIT", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "TransparentOITComposite: not initialized");

        const auto &hdr = tex(bb, rg, "HDR.Color");
        const auto &acc = tex(bb, rg, "Trans.Accum");
        const auto &rev = tex(bb, rg, "Trans.Reveal");
        const auto &out = tex(bb, rg, "HDR.OIT");

        NYX_ASSERT(hdr.tex && acc.tex && rev.tex && out.tex,
                   "TransparentOITComposite: missing textures");

        glUseProgram(m_prog);

        glBindTextureUnit(0, hdr.tex);
        glBindTextureUnit(1, acc.tex);
        glBindTextureUnit(2, rev.tex);
        glBindImageTexture(3, out.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);

        const uint32_t gx = (rc.fbWidth + 15u) / 16u;
        const uint32_t gy = (rc.fbHeight + 15u) / 16u;
        glDispatchCompute(gx, gy, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
