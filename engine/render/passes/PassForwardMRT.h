#pragma once

#include "render/passes/RenderPass.h"
#include <functional>

namespace Nyx {

class PassForwardMRT final : public RenderPass {
public:
  void configure(uint32_t fbo, uint32_t forwardProg,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_forwardProg = 0;
  std::function<void(ProcMeshType)> m_draw;
};

} // namespace Nyx
