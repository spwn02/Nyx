#pragma once

#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"

namespace Nyx {

class GLFullscreenTriangle;

class PassPresent final : public RenderPass {
public:
  ~PassPresent() override;

  void configure(GLShaderUtil &shaders, GLFullscreenTriangle &fsTri);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  GLFullscreenTriangle *m_fsTri = nullptr;
};

} // namespace Nyx
