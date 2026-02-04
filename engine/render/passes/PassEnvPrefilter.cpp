#include "PassEnvPrefilter.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "env/EnvironmentIBL.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderGraph.h"
#include "render/rg/RenderPassContext.h"

#include <algorithm>
#include <glad/glad.h>

namespace Nyx {

static inline uint32_t ceilDiv(uint32_t x, uint32_t d) {
  return (x + d - 1u) / d;
}

PassEnvPrefilter::~PassEnvPrefilter() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassEnvPrefilter::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("env_prefilter.comp");
  NYX_ASSERT(m_prog != 0, "PassEnvPrefilter: shader build failed");
}

void PassEnvPrefilter::setup(RenderGraph &graph, const RenderPassContext &ctx,
                             const RenderableRegistry &registry,
                             EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "EnvPrefilter",
      [&](RenderPassBuilder &b) { (void)b; },
      [&](const RenderPassContext &, RenderResourceBlackboard &,
          RGResources &) {
        NYX_ASSERT(m_prog != 0, "PassEnvPrefilter: missing program");

        auto &env = engine.envIBL();
        if (!env.dirty())
          return;

        env.ensureResources();

        const uint32_t envTex = env.envCube();
        const uint32_t preTex = env.envPrefilteredCube();
        NYX_ASSERT(envTex != 0 && preTex != 0,
                   "PassEnvPrefilter: missing env textures");

        GLint baseSize = 0;
        glGetTextureLevelParameteriv(preTex, 0, GL_TEXTURE_WIDTH, &baseSize);
        NYX_ASSERT(baseSize > 0, "PassEnvPrefilter: invalid prefilter size");

        const uint32_t mipCount =
            EnvironmentIBL::mipCountForSize(static_cast<uint32_t>(baseSize));

        glUseProgram(m_prog);

        const int locRough = glGetUniformLocation(m_prog, "u_Roughness");
        const int locSamp = glGetUniformLocation(m_prog, "u_SampleCount");

        glBindTextureUnit(0, envTex);

        for (uint32_t mip = 0; mip < mipCount; ++mip) {
          const uint32_t s = std::max(1u, (uint32_t)baseSize >> mip);
          const float rough = (mipCount <= 1)
                                  ? 0.0f
                                  : float(mip) / float(mipCount - 1);

          if (locRough >= 0)
            glUniform1f(locRough, rough);
          if (locSamp >= 0)
            glUniform1ui(locSamp, (mip == 0) ? 1024u : 256u);

          glBindImageTexture(1, preTex, (int)mip, GL_TRUE, 0, GL_WRITE_ONLY,
                             GL_RGBA16F);

          glDispatchCompute(ceilDiv(s, 8u), ceilDiv(s, 8u), 6);

          glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                          GL_TEXTURE_FETCH_BARRIER_BIT);
        }
      });
}

} // namespace Nyx
