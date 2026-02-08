#pragma once

#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"

namespace Nyx {

class PassTransparentOITComposite final : public RenderPass {
public:
  ~PassTransparentOITComposite() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_prog = 0;
};

} // namespace Nyx
