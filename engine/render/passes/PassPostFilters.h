#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"

namespace Nyx {

class GLFullscreenTriangle;

class PassPostFilters final : public RenderPass {
public:
  ~PassPostFilters() override;

  void configure(GLShaderUtil &shaders, GLResources &res,
                 GLFullscreenTriangle &fsTri);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
  GLFullscreenTriangle *m_fsTri = nullptr;
};

} // namespace Nyx
