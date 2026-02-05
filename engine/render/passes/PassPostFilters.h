#pragma once

#include "RenderPass.h"
#include "render/gl/GLShaderUtil.h"

namespace Nyx {

class PassPostFilters final : public RenderPass {
public:
  ~PassPostFilters() override;

  void configure(GLShaderUtil &shaders);
  void setSSBO(uint32_t ssbo) { m_filterSSBO = ssbo; }

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_prog = 0;
  uint32_t m_filterSSBO = 0;
};

} // namespace Nyx
