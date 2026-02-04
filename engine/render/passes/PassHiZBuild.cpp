#include "PassHiZBuild.h"

#include "core/Assert.h"
#include "render/gl/GLShaderUtil.h"

#include <algorithm>
#include <glad/glad.h>

namespace Nyx {

static uint32_t hizMipCount(uint32_t w, uint32_t h) {
  uint32_t m = 1;
  uint32_t mx = std::max(w, h);
  while (mx > 1) {
    mx >>= 1;
    ++m;
  }
  return m;
}

PassHiZBuild::~PassHiZBuild() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassHiZBuild::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("passes/hiz_build.comp");
  NYX_ASSERT(m_prog != 0, "PassHiZBuild: shader build failed");
}

void PassHiZBuild::setup(RenderGraph &graph, const RenderPassContext &ctx,
                         const RenderableRegistry &registry,
                         EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;
  (void)editorVisible;

  graph.addPass(
      "HiZBuild",
      [&](RenderPassBuilder &b) {
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.writeTexture("HiZ.Depth", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &depth = tex(bb, rg, "Depth.Pre");
        const auto &hiz = tex(bb, rg, "HiZ.Depth");
        NYX_ASSERT(depth.tex && hiz.tex, "PassHiZBuild: missing textures");
        NYX_ASSERT(m_prog != 0, "PassHiZBuild: not initialized");

        glUseProgram(m_prog);
        glBindTextureUnit(0, depth.tex);

        const uint32_t mips = hizMipCount(rc.fbWidth, rc.fbHeight);
        for (uint32_t mip = 0; mip < mips; ++mip) {
          const uint32_t w = std::max(1u, rc.fbWidth >> mip);
          const uint32_t h = std::max(1u, rc.fbHeight >> mip);

          const GLint locMip = glGetUniformLocation(m_prog, "uMip");
          const GLint locBase = glGetUniformLocation(m_prog, "uBaseSize");
          if (locMip >= 0)
            glUniform1ui(locMip, mip);
          if (locBase >= 0)
            glUniform2ui(locBase, rc.fbWidth, rc.fbHeight);

          glBindImageTexture(1, hiz.tex, (int)mip, GL_FALSE, 0, GL_WRITE_ONLY,
                             GL_R32F);

          const uint32_t gx = (w + 15u) / 16u;
          const uint32_t gy = (h + 15u) / 16u;
          glDispatchCompute(gx, gy, 1);

          glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                          GL_TEXTURE_FETCH_BARRIER_BIT);
        }
      });
}

} // namespace Nyx
