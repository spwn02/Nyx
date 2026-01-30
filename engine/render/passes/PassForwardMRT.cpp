#include "PassForwardMRT.h"

#include "app/EngineContext.h"
#include "core/Assert.h"

#include <array>
#include <glad/glad.h>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kLightsBinding = 16;
static constexpr uint32_t kShadowDirBinding = 17;
static constexpr uint32_t kShadowSpotBinding = 18;

void PassForwardMRT::configure(uint32_t fbo, uint32_t forwardProg,
                               std::function<void(ProcMeshType)> drawFn) {
  m_fbo = fbo;
  m_forwardProg = forwardProg;
  m_draw = std::move(drawFn);
}

void PassForwardMRT::setup(RenderGraph &graph, const RenderPassContext &ctx,
                           const RenderableRegistry &registry,
                           EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "ForwardMRT",
      [&](RenderPassBuilder &b) {
        b.writeTexture("HDR.Color", RenderAccess::ColorWrite);
        b.writeTexture("ID.Submesh", RenderAccess::ColorWrite);
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &hdrT = tex(bb, rg, "HDR.Color");
        const auto &idT = tex(bb, rg, "ID.Submesh");
        const auto &depT = tex(bb, rg, "Depth.Pre");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, hdrT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT1, idT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const std::array<GLenum, 2> bufs{GL_COLOR_ATTACHMENT0,
                                         GL_COLOR_ATTACHMENT1};
        glNamedFramebufferDrawBuffers(m_fbo, (GLsizei)bufs.size(), bufs.data());

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "ForwardMRT framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_EQUAL);
        glDepthMask(GL_FALSE);

        const float clearC[4] = {0.1f, 0.1f, 0.2f, 0.0f};
        glClearBufferfv(GL_COLOR, 0, clearC);

        const uint32_t clearID[1] = {0u};
        glClearBufferuiv(GL_COLOR, 1, clearID);

        engine.materials().uploadIfDirty();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightsBinding,
                         engine.lights().ssbo());

        glUseProgram(m_forwardProg);

        const int locVP = glGetUniformLocation(m_forwardProg, "u_ViewProj");
        const int locV = glGetUniformLocation(m_forwardProg, "u_View");
        const int locM = glGetUniformLocation(m_forwardProg, "u_Model");
        const int locPick = glGetUniformLocation(m_forwardProg, "u_PickID");
        const int locVM = glGetUniformLocation(m_forwardProg, "u_ViewMode");
        const int locMat =
            glGetUniformLocation(m_forwardProg, "u_MaterialIndex");
        const int locSplits =
            glGetUniformLocation(m_forwardProg, "u_CSM_Splits");
        const int locDirSMSize =
            glGetUniformLocation(m_forwardProg, "u_ShadowDirSize");
        const int locSpotSMSize =
            glGetUniformLocation(m_forwardProg, "u_ShadowSpotSize");
        const int locIsLight =
            glGetUniformLocation(m_forwardProg, "u_IsLight");
        const int locLightColorInt =
            glGetUniformLocation(m_forwardProg, "u_LightColorIntensity");
        const int locLightExposure =
            glGetUniformLocation(m_forwardProg, "u_LightExposure");

        glUniform1ui(locVM, static_cast<uint32_t>(engine.viewMode()));
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        if (locV >= 0)
          glUniformMatrix4fv(locV, 1, GL_FALSE, &rc.view[0][0]);
        if (locSplits >= 0) {
          const auto &splits = engine.shadows().csmSplits();
          glUniform4fv(locSplits, 1, &splits[0]);
        }
        if (locDirSMSize >= 0)
          glUniform2f(locDirSMSize,
                      (float)engine.shadows().dirShadowSize(),
                      (float)engine.shadows().dirShadowSize());
        if (locSpotSMSize >= 0)
          glUniform2f(locSpotSMSize,
                      (float)engine.shadows().spotShadowSize(),
                      (float)engine.shadows().spotShadowSize());
        glBindTextureUnit(6, engine.shadows().dirShadowTex());
        glBindTextureUnit(7, engine.shadows().spotShadowTex());
        glBindTextureUnit(8, engine.shadows().pointShadowTex());

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kShadowDirBinding,
                         engine.shadows().dirMatricesSSBO());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kShadowSpotBinding,
                         engine.shadows().spotMatricesSSBO());

        for (const auto &r : registry.all()) {
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera) {
            glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_FALSE);
          } else {
            glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
          }
          glUniformMatrix4fv(locM, 1, GL_FALSE, &r.model[0][0]);
          glUniform1ui(locPick, r.pickID);
          glUniform1ui(locMat, engine.materialIndex(r));
          if (locIsLight >= 0)
            glUniform1i(locIsLight, r.isLight ? 1 : 0);
          if (r.isLight) {
            if (locLightColorInt >= 0) {
              glUniform4f(locLightColorInt, r.lightColor.x, r.lightColor.y,
                          r.lightColor.z, r.lightIntensity);
            }
            if (locLightExposure >= 0)
              glUniform1f(locLightExposure, r.lightExposure);
          }
          if (m_draw)
            m_draw(r.mesh);
        }
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
      });
}

} // namespace Nyx
