#include "PassEnvEquirectToCube.h"

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

PassEnvEquirectToCube::~PassEnvEquirectToCube() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassEnvEquirectToCube::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("env_equirect_to_cube.comp");
  NYX_ASSERT(m_prog != 0, "PassEnvEquirectToCube: shader build failed");
}

void PassEnvEquirectToCube::setup(RenderGraph &graph,
                                  const RenderPassContext &ctx,
                                  const RenderableRegistry &registry,
                                  EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "EnvEquirectToCube",
      [&](RenderPassBuilder &b) { (void)b; },
      [&](const RenderPassContext &, RenderResourceBlackboard &,
          RGResources &) {
        NYX_ASSERT(m_prog != 0, "PassEnvEquirectToCube: missing program");

        auto &env = engine.envIBL();
        if (!env.dirty())
          return;

        const uint32_t hdrTex = env.hdrEquirect();
        if (hdrTex == 0)
          return;

        env.ensureResources();

        const uint32_t cubeTex = env.envCube();
        NYX_ASSERT(cubeTex != 0, "PassEnvEquirectToCube: Env.Cube tex=0");

        GLint size = 0;
        glGetTextureLevelParameteriv(cubeTex, 0, GL_TEXTURE_WIDTH, &size);
        NYX_ASSERT(size > 0, "PassEnvEquirectToCube: invalid cube size");

        glUseProgram(m_prog);

        glBindTextureUnit(0, hdrTex);
        glBindImageTexture(1, cubeTex, 0, GL_TRUE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);

        const uint32_t s = static_cast<uint32_t>(size);
        const uint32_t gx = ceilDiv(s, 8u);
        const uint32_t gy = ceilDiv(s, 8u);
        glDispatchCompute(gx, gy, 6);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);

        glGenerateTextureMipmap(cubeTex);
      });
}

} // namespace Nyx
