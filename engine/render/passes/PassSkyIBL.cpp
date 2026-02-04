#include "PassSkyIBL.h"

#include "app/EngineContext.h"
#include "core/Assert.h"
#include "render/rg/RGResources.h"
#include "render/rg/RenderGraph.h"
#include "render/rg/RenderPassContext.h"

#include "glm/gtc/matrix_inverse.hpp"
#include <glad/glad.h>

namespace Nyx {

static inline uint32_t ceilDiv(uint32_t x, uint32_t d) {
  return (x + d - 1u) / d;
}

PassSkyIBL::~PassSkyIBL() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassSkyIBL::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("sky_ibl.comp");
  NYX_ASSERT(m_prog != 0, "PassSkyIBL: shader build failed");
}

void PassSkyIBL::setup(RenderGraph &graph, const RenderPassContext &ctx,
                       const RenderableRegistry &registry,
                       EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "SkyIBL",
      [&](RenderPassBuilder &b) {
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.writeTexture("HDR.Color", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PassSkyIBL: missing program");

        const auto &depth = tex(bb, rg, "Depth.Pre");
        const auto &hdr = tex(bb, rg, "HDR.Color");
        NYX_ASSERT(depth.tex && hdr.tex, "PassSkyIBL: missing textures");

        auto &env = engine.envIBL();
        if (!env.ready() || env.envCube() == 0)
          return;

        glUseProgram(m_prog);

        // Sky UBO is already bound at binding point 2 by EngineContext
        // Just bind textures
        glBindTextureUnit(0, env.envCube());
        glBindImageTexture(1, hdr.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);
        glBindTextureUnit(3, depth.tex);

        const uint32_t gx = ceilDiv(rc.fbWidth, 16u);
        const uint32_t gy = ceilDiv(rc.fbHeight, 16u);
        glDispatchCompute(gx, gy, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
