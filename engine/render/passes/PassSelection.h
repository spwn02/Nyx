#pragma once

#include "render/passes/RenderPass.h"

namespace Nyx {

class GLFullscreenTriangle;

class PassSelection final : public RenderPass {
public:
  void configure(uint32_t fbo, uint32_t outlineProg,
                 GLFullscreenTriangle *fsTri, uint32_t selectedSSBO);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_outlineProg = 0;
  GLFullscreenTriangle *m_fsTri = nullptr;
  uint32_t m_selectedSSBO = 0;
};

} // namespace Nyx
