#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"

namespace Nyx {

class GLFullscreenTriangle;

class PassSelection final : public RenderPass {
public:
  ~PassSelection() override;

  void updateSelectedIDs(const std::vector<uint32_t> &ids,
                         uint32_t activePick);

  void configure(GLShaderUtil &shaders, GLResources &res,
                 GLFullscreenTriangle &fsTri);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  GLFullscreenTriangle *m_fsTri = nullptr;
  GLResources *m_res = nullptr;
  uint32_t m_selectedSSBO = 0, m_selectedCount = 0;
};

} // namespace Nyx
