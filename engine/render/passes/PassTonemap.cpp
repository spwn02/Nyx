#include "PassTonemap.h"
#include "../../core/Assert.h"
#include "render/gl/GLShaderUtil.h"

#include <glad/glad.h>

namespace Nyx {

PassTonemap::~PassTonemap() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassTonemap::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("tonemap.comp");
}

void PassTonemap::setup(RenderGraph &graph, const RenderPassContext &ctx,
                        const RenderableRegistry &registry,
                        EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "Tonemap",
      [&](RenderPassBuilder &b) {
        b.readTexture("HDR.Debug", RenderAccess::SampledRead);
        b.writeTexture("Post.In", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &hdrTex = tex(bb, rg, "HDR.Debug").tex;
        const auto &ldrTex = tex(bb, rg, "Post.In").tex;
        float exposure = 1.0f;
        bool applyGamma = true;

        NYX_ASSERT(m_prog != 0, "PassTonemap not initialized");
        NYX_ASSERT(hdrTex != 0 && ldrTex != 0, "PassTonemap invalid textures");

        glUseProgram(m_prog);

        // uniforms
        const GLint locExp = glGetUniformLocation(m_prog, "u_Exposure");
        const GLint locGam = glGetUniformLocation(m_prog, "u_ApplyGamma");
        if (locExp >= 0)
          glUniform1f(locExp, exposure);
        if (locGam >= 0)
          glUniform1i(locGam, applyGamma ? 1 : 0);

        // input sampler
        glBindTextureUnit(0, hdrTex);

        // output image
        glBindImageTexture(1, ldrTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        const uint32_t gx = (rc.fbWidth + 15u) / 16u;
        const uint32_t gy = (rc.fbHeight + 15u) / 16u;
        glDispatchCompute(gx, gy, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
