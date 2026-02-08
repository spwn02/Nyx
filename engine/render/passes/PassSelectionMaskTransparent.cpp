#include "PassSelectionMaskTransparent.h"

#include "app/EngineContext.h"
#include "core/Assert.h"

#include <glad/glad.h>
#include <unordered_set>

namespace Nyx {

PassSelectionMaskTransparent::~PassSelectionMaskTransparent() {
  if (m_prog != 0) {
    glDeleteProgram(m_prog);
    m_prog = 0;
  }
  if (m_fbo != 0 && m_res) {
    m_res->releaseFBO(m_fbo);
    m_fbo = 0;
  }
}

void PassSelectionMaskTransparent::updateSelectedIDs(
    const std::vector<uint32_t> &ids) {
  m_selected = ids;
}

void PassSelectionMaskTransparent::configure(GLShaderUtil &shaders,
                                             GLResources &res,
                                             std::function<void(ProcMeshType)>
                                                 drawFn) {
  m_res = &res;
  m_fbo = res.acquireFBO();
  m_prog =
      shaders.buildProgramVF("selection_mask_transparent.vert",
                             "selection_mask_transparent.frag");
  m_draw = std::move(drawFn);
}

void PassSelectionMaskTransparent::setup(
    RenderGraph &graph, const RenderPassContext &ctx,
    const RenderableRegistry &registry, EngineContext &engine,
    bool editorVisible) {
  (void)ctx;
  (void)editorVisible;

  graph.addPass(
      "SelectionMaskTransparent",
      [&](RenderPassBuilder &b) {
        b.readTexture("Depth.Pre", RenderAccess::SampledRead);
        b.writeTexture("Mask.SelectedTrans", RenderAccess::ColorWrite);
      },
      [&](const RenderPassContext &rc, RenderResourceBlackboard &bb,
          RGResources &rg) {
        const auto &maskT = tex(bb, rg, "Mask.SelectedTrans");
        const auto &depT = tex(bb, rg, "Depth.Pre");

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glNamedFramebufferTexture(m_fbo, GL_COLOR_ATTACHMENT0, maskT.tex, 0);
        glNamedFramebufferTexture(m_fbo, GL_DEPTH_ATTACHMENT, depT.tex, 0);

        const GLenum drawBuf = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(m_fbo, 1, &drawBuf);

        NYX_ASSERT(glCheckNamedFramebufferStatus(m_fbo, GL_FRAMEBUFFER) ==
                       GL_FRAMEBUFFER_COMPLETE,
                   "SelectionMaskTransparent framebuffer incomplete");

        glViewport(0, 0, (int)rc.fbWidth, (int)rc.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);

        const uint32_t clear0[1] = {0u};
        glClearBufferuiv(GL_COLOR, 0, clear0);

        if (m_selected.empty())
          return;

        std::unordered_set<uint32_t> selected;
        selected.reserve(m_selected.size() * 2 + 1);
        for (uint32_t id : m_selected)
          selected.insert(id);

        glUseProgram(m_prog);
        const int locVP = glGetUniformLocation(m_prog, "u_ViewProj");
        glUniformMatrix4fv(locVP, 1, GL_FALSE, &rc.viewProj[0][0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13,
                         engine.perDraw().ssbo());

        const auto &drawList = registry.transparentSorted();
        const uint32_t baseOffset = engine.perDrawTransparentOffset();
        uint32_t visibleIdx = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawList.size()); ++i) {
          const auto &r = drawList[i];
          if (engine.isEntityHidden(r.entity))
            continue;
          if (r.isCamera)
            continue;
          if (selected.find(r.pickID) == selected.end())
            continue;
          engine.rendererDrawPrimitive(static_cast<uint32_t>(r.mesh),
                                       baseOffset + visibleIdx);
          visibleIdx++;
        }

        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
      });
}

} // namespace Nyx
