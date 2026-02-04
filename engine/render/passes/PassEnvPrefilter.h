#pragma once

#include "RenderPass.h"
#include "render/gl/GLShaderUtil.h"

namespace Nyx {

class PassEnvPrefilter : public RenderPass {
public:
  ~PassEnvPrefilter() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;
};

} // namespace Nyx
