#pragma once

#include "render/passes/RenderPass.h"

namespace Nyx {

class GLShaderUtil;

class PassHiZBuild final : public RenderPass {
public:
  ~PassHiZBuild() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_prog = 0;
};

} // namespace Nyx
