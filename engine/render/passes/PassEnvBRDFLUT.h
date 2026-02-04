#pragma once

#include "RenderPass.h"
#include "render/gl/GLShaderUtil.h"

namespace Nyx {

class PassEnvBRDFLUT : public RenderPass {
public:
  ~PassEnvBRDFLUT() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;
};

} // namespace Nyx
