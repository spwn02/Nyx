#pragma once

#include "RenderPass.h"
#include "render/gl/GLShaderUtil.h"

namespace Nyx {

class PassEnvEquirectToCube : public RenderPass {
public:
  ~PassEnvEquirectToCube() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;
};

} // namespace Nyx
