#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"
#include "scene/RenderableRegistry.h"

#include <cstdint>

namespace Nyx {

class EngineContext;

class PassTransparentOIT final : public RenderPass {
public:
  ~PassTransparentOIT() override;

  void configure(GLShaderUtil &shaders, GLResources &res);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_prog = 0;
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
};

} // namespace Nyx
