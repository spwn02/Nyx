#include "PassShadowDebugOverlay.h"

#include "app/EngineContext.h"
#include "core/Assert.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace Nyx {

PassShadowDebugOverlay::~PassShadowDebugOverlay() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
}

void PassShadowDebugOverlay::configure(GLShaderUtil &shaders) {
  m_prog = shaders.buildProgramC("passes/shadow_debug.comp");
  NYX_ASSERT(m_prog != 0, "PassShadowDebugOverlay: shader build failed");
}

void PassShadowDebugOverlay::setup(RenderGraph &graph,
                                   const RenderPassContext &ctx,
                                   const RenderableRegistry &registry,
                                   EngineContext &engine,
                                   bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)editorVisible;

  graph.addPass(
      "ShadowDebugOverlay",
      [&](RenderPassBuilder &b) {
        if (engine.transparencyMode() == TransparencyMode::OIT) {
          b.readTexture("HDR.OIT", RenderAccess::SampledRead);
        } else {
          b.readTexture("HDR.Color", RenderAccess::SampledRead);
        }
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.readTexture("Shadow.CSMAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.SpotAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.DirAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.PointArray", RenderAccess::SampledRead);
        b.writeTexture("HDR.Debug", RenderAccess::ImageWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        NYX_ASSERT(m_prog != 0, "PassShadowDebugOverlay: not initialized");

        const auto &hdrIn =
            (engine.transparencyMode() == TransparencyMode::OIT)
                ? tex(bb, rg, "HDR.OIT")
                : tex(bb, rg, "HDR.Color");
        const auto &depth = tex(bb, rg, "Depth.Pre");
        const auto &outDbg = tex(bb, rg, "HDR.Debug");
        const auto &csmAtlas = tex(bb, rg, "Shadow.CSMAtlas");
        const auto &spotAtlas = tex(bb, rg, "Shadow.SpotAtlas");
        const auto &dirAtlas = tex(bb, rg, "Shadow.DirAtlas");
        const auto &pointArray = tex(bb, rg, "Shadow.PointArray");

        NYX_ASSERT(hdrIn.tex && depth.tex && outDbg.tex && csmAtlas.tex,
                   "PassShadowDebugOverlay: missing textures");

        glUseProgram(m_prog);

        glBindTextureUnit(0, hdrIn.tex);
        glBindTextureUnit(1, depth.tex);
        glBindTextureUnit(6, csmAtlas.tex);
        glBindTextureUnit(7, spotAtlas.tex);
        glBindTextureUnit(8, dirAtlas.tex);
        glBindTextureUnit(9, pointArray.tex);

        glBindImageTexture(2, outDbg.tex, 0, GL_FALSE, 0, GL_WRITE_ONLY,
                           GL_RGBA16F);

        glBindBufferBase(GL_UNIFORM_BUFFER, 5, engine.shadowCSMUBO());

        const glm::mat4 invViewProj = glm::inverse(rc.viewProj);
        const glm::mat4 view = rc.view;
        const glm::vec3 camPos = rc.cameraPos;

        const GLint locInvVP = glGetUniformLocation(m_prog, "u_InvViewProj");
        const GLint locView = glGetUniformLocation(m_prog, "u_View");
        const GLint locCam = glGetUniformLocation(m_prog, "u_CamPos");
        const GLint locMode = glGetUniformLocation(m_prog, "u_Mode");
        const GLint locAlpha = glGetUniformLocation(m_prog, "u_Alpha");
        if (locInvVP >= 0)
          glUniformMatrix4fv(locInvVP, 1, GL_FALSE, &invViewProj[0][0]);
        if (locView >= 0)
          glUniformMatrix4fv(locView, 1, GL_FALSE, &view[0][0]);
        if (locCam >= 0)
          glUniform3f(locCam, camPos.x, camPos.y, camPos.z);
        if (locMode >= 0)
          glUniform1ui(locMode, static_cast<uint32_t>(m_mode));
        if (locAlpha >= 0)
          glUniform1f(locAlpha, m_alpha);

        const uint32_t gx = (rc.fbWidth + 15u) / 16u;
        const uint32_t gy = (rc.fbHeight + 15u) / 16u;
        glDispatchCompute(gx, gy, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT);
      });
}

} // namespace Nyx
