#pragma once

#include "render/passes/RenderPass.h"
#include <functional>

namespace Nyx {

class GLFullscreenTriangle;

class PassPostFilters final : public RenderPass {
public:
  void configure(uint32_t fbo, uint32_t presentProg,
                 GLFullscreenTriangle *fsTri);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_presentProg = 0;
  GLFullscreenTriangle *m_fsTri = nullptr;
};

} // namespace Nyx
