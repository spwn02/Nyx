#pragma once

#include "render/passes/RenderPass.h"
#include "render/passes/PassTonemap.h"

namespace Nyx {

class PassTonemapRG final : public RenderPass {
public:
  void init();
  void shutdown();

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  PassTonemap m_tonemap;
};

} // namespace Nyx
