#include "PassSelection.h"

#include "core/Assert.h"
#include "render/gl/GLFullscreenTriangle.h"
#include "render/gl/GLResources.h"

#include <glad/glad.h>

namespace Nyx {

static constexpr uint32_t kSelectedIDsBinding = 15;

PassSelection::~PassSelection() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }

  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }

  if (m_selectedSSBO) {
    glDeleteBuffers(1, &m_selectedSSBO);
  }
}

void PassSelection::updateSelectedIDs(const std::vector<uint32_t> &ids) {
  const uint32_t count = (uint32_t)ids.size();
  std::vector<uint32_t> tmp;
  tmp.reserve(count + 1);
  tmp.push_back(count);
  for (uint32_t v : ids)
    tmp.push_back(v);

  glNamedBufferData(m_selectedSSBO, (GLsizeiptr)(tmp.size() * sizeof(uint32_t)),
                    tmp.data(), GL_DYNAMIC_DRAW);
  m_selectedCount = count;
}

void PassSelection::configure(GLShaderUtil &shaders, GLResources &res,
                              GLFullscreenTriangle &fsTri) {
  m_fsTri = &fsTri;
  m_prog = shaders.buildProgramVF("fullscreen.vert", "outline.frag");

  m_res = &res;
  m_fbo = res.acquireFBO();

  glCreateBuffers(1, &m_selectedSSBO);
  std::vector<uint32_t> init{0u}; // selectedCount=0
  glNamedBufferData(m_selectedSSBO,
                    (GLsizeiptr)(init.size() * sizeof(uint32_t)), init.data(),
                    GL_DYNAMIC_DRAW);
}

void PassSelection::setup(RenderGraph &graph, const RenderPassContext &ctx,
                          const RenderableRegistry &registry,
                          EngineContext &engine, bool editorVisible) {
  (void)ctx;
  (void)registry;
  (void)engine;

  graph.addPass(
      "Selection",
      [&](RenderPassBuilder &b) {
        b.readTexture("LDR.Color", RenderAccess::SampledRead);
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.readTexture("ID.Submesh", RenderAccess::SampledRead);
        b.writeTexture("OUT.Color", RenderAccess::ColorWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &ldrT = tex(bb, rg, "LDR.Color");
        const auto &depT = tex(bb, rg, "Depth.Pre");
        const auto &idT = tex(bb, rg, "ID.Submesh");
        const auto &outT = tex(bb, rg, "OUT.Color");

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kSelectedIDsBinding,
                         m_selectedSSBO);

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, outT.tex, 0);

        const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuf);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "Selection framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glDisable(GL_DEPTH_TEST);

        glUseProgram(m_prog);
        if (m_fsTri)
          glBindVertexArray(m_fsTri->vao);

        const int locFlip = glGetUniformLocation(m_prog, "u_FlipY");
        if (locFlip >= 0)
          glUniform1i(locFlip, 0);

        glBindTextureUnit(0, ldrT.tex); // uSceneColor
        glBindTextureUnit(1, depT.tex); // uDepth
        glBindTextureUnit(2, idT.tex);  // uID

        glDrawArrays(GL_TRIANGLES, 0, 3);
      });
}

} // namespace Nyx
