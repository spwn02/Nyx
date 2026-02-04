#include "PassForwardMRT.h"

#include "app/EngineContext.h"
#include "core/Assert.h"

#include <array>
#include <glad/glad.h>

namespace Nyx {

static constexpr uint32_t kMaterialsBinding = 14;
static constexpr uint32_t kLightsBinding = 20;

PassForwardMRT::~PassForwardMRT() {
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
  if (m_forwardProg != 0) {
    glDeleteProgram(m_forwardProg);
    m_forwardProg = 0;
  }
}

void PassForwardMRT::configure(GLShaderUtil &shader, GLResources &res,
                               std::function<void(ProcMeshType)> drawFn) {
  m_res = &res;

  m_fbo = res.acquireFBO();
  m_forwardProg = shader.buildProgramVF("forward_mrt.vert", "forward_mrt.frag");
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
        b.readTexture("Shadow.CSMAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.SpotAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.DirAtlas", RenderAccess::SampledRead);
        b.readTexture("Shadow.PointArray", RenderAccess::SampledRead);
        b.readBuffer("Scene.Lights", RenderAccess::SSBORead);
        b.readBuffer("LightGrid.Meta", RenderAccess::UBORead);
        b.readBuffer("LightGrid.Header", RenderAccess::SSBORead);
        b.readBuffer("LightGrid.Indices", RenderAccess::SSBORead);
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

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialsBinding,
                         engine.materials().ssbo());
        const auto &lights = buf(bb, rg, "Scene.Lights");
        const auto &gridMeta = buf(bb, rg, "LightGrid.Meta");
        const auto &gridHeader = buf(bb, rg, "LightGrid.Header");
        const auto &gridIndices = buf(bb, rg, "LightGrid.Indices");

        if (lights.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kLightsBinding, lights.buf);
        if (gridMeta.buf)
          glBindBufferBase(GL_UNIFORM_BUFFER, 22, gridMeta.buf);
        if (gridHeader.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 24, gridHeader.buf);
        if (gridIndices.buf)
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 25, gridIndices.buf);

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
        const int locIsLight = glGetUniformLocation(m_forwardProg, "u_IsLight");
        const int locLightColorInt =
            glGetUniformLocation(m_forwardProg, "u_LightColorIntensity");
        const int locLightExposure =
            glGetUniformLocation(m_forwardProg, "u_LightExposure");
        const int locHasIBL = glGetUniformLocation(m_forwardProg, "u_HasIBL");

        glUniform1ui(locVM, static_cast<uint32_t>(engine.viewMode()));
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        if (locV >= 0)
          glUniformMatrix4fv(locV, 1, GL_FALSE, &rc.view[0][0]);

        const auto &csmAtlas = tex(bb, rg, "Shadow.CSMAtlas");
        const auto &spotAtlas = tex(bb, rg, "Shadow.SpotAtlas");
        const auto &dirAtlas = tex(bb, rg, "Shadow.DirAtlas");
        const auto &pointArray = tex(bb, rg, "Shadow.PointArray");
        
        if (csmAtlas.tex)
          glBindTextureUnit(6, csmAtlas.tex);
        if (spotAtlas.tex)
          glBindTextureUnit(7, spotAtlas.tex);
        if (dirAtlas.tex)
          glBindTextureUnit(8, dirAtlas.tex);
        if (pointArray.tex)
          glBindTextureUnit(9, pointArray.tex);

        // CSM UBO binding=5 (set by PassShadowCSM)
        glBindBufferBase(GL_UNIFORM_BUFFER, 5, engine.shadowCSMUBO());

        // Shadow metadata UBO binding=10
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, engine.lights().shadowMetadataUBO());

        // Sky UBO is already bound at binding point 2 by EngineContext

        auto &env = engine.envIBL();
        if (env.ready()) {
          glBindTextureUnit(0, env.envIrradianceCube());
          glBindTextureUnit(1, env.envPrefilteredCube());
          glBindTextureUnit(2, env.brdfLUT());
        }
        if (locHasIBL >= 0)
          glUniform1i(locHasIBL, env.ready() ? 1 : 0);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14,
                         engine.materials().ssbo());

        // Bind material texture table at unit 10 (matches forward_mrt.frag).
        // Clamp to 16 samplers to stay under common limits.
        engine.materials().textures().bindAll(10, 16);

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
