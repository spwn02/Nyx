#include "PassEnvIrradiance.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderGraph.h"
#include "render/rg/RenderPassContext.h"

#include <glad/glad.h>

namespace Nyx {

static inline uint32_t ceilDiv(uint32_t x, uint32_t d) {
  return (x + d - 1u) / d;
}

PassEnvIrradiance::~PassEnvIrradiance() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassEnvIrradiance::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("env_irradiance.comp");
  NYX_ASSERT(m_prog != 0, "PassEnvIrradiance: shader build failed");
}

void PassEnvIrradiance::setup(RenderGraph &graph, const RenderPassContext &ctx,
                              const RenderableRegistry &registry,
                              EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "EnvIrradiance",
      [&](RenderPassBuilder &b) {
        (void)b;
      },
      [&](const RenderPassContext &, RenderResourceBlackboard &,
          RGResources &) {
        NYX_ASSERT(m_prog != 0, "PassEnvIrradiance: missing program");

        auto &env = engine.envIBL();
        if (!env.dirty())
          return;
        if (env.hdrEquirect() == 0)
          return;

        env.ensureResources();

        const uint32_t envTex = env.envCube();
        const uint32_t irrTex = env.envIrradianceCube();
        if (envTex == 0 || irrTex == 0) {
          // Sky/IBL may be intentionally unset. Skip without asserting.
          return;
        }

        GLint size = 0;
        glGetTextureLevelParameteriv(irrTex, 0, GL_TEXTURE_WIDTH, &size);
        NYX_ASSERT(size > 0, "PassEnvIrradiance: invalid irradiance size");

        glUseProgram(m_prog);

        glBindTextureUnit(0, envTex);
        glBindImageTexture(1, irrTex, 0, GL_TRUE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);

        const uint32_t s = static_cast<uint32_t>(size);
        glDispatchCompute(ceilDiv(s, 8u), ceilDiv(s, 8u), 6);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
