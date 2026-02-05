#include "PassEnvBRDFLUT.h"

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

PassEnvBRDFLUT::~PassEnvBRDFLUT() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassEnvBRDFLUT::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("env_brdf_lut.comp");
  NYX_ASSERT(m_prog != 0, "PassEnvBRDFLUT: shader build failed");
}

void PassEnvBRDFLUT::setup(RenderGraph &graph, const RenderPassContext &ctx,
                           const RenderableRegistry &registry,
                           EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "EnvBRDFLUT",
      [&](RenderPassBuilder &b) { (void)b; },
      [&](const RenderPassContext &, RenderResourceBlackboard &,
          RGResources &) {
        NYX_ASSERT(m_prog != 0, "PassEnvBRDFLUT: missing program");

        auto &env = engine.envIBL();
        if (!env.dirty())
          return;
        if (env.hdrEquirect() == 0)
          return;

        env.ensureResources();

        const uint32_t lutTex = env.brdfLUT();
        if (lutTex == 0) {
          // Sky/IBL may be intentionally unset. Skip without asserting.
          return;
        }

        GLint size = 0;
        glGetTextureLevelParameteriv(lutTex, 0, GL_TEXTURE_WIDTH, &size);
        NYX_ASSERT(size > 0, "PassEnvBRDFLUT: invalid LUT size");

        glUseProgram(m_prog);

        glBindImageTexture(0, lutTex, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);

        const uint32_t s = static_cast<uint32_t>(size);
        glDispatchCompute(ceilDiv(s, 8u), ceilDiv(s, 8u), 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);

        env.markBuilt();
      });
}

} // namespace Nyx
