#pragma once

#include "render/passes/RenderPass.h"
#include <functional>

namespace Nyx {

class PassShadowDir final : public RenderPass {
public:
  void configure(uint32_t fbo, uint32_t shadowProg,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_shadowProg = 0;
  std::function<void(ProcMeshType)> m_draw;

  float m_orthoHalfSize = 25.0f;
  float m_nearZ = 0.1f;
  float m_farZ = 200.0f;
};

} // namespace Nyx
