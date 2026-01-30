#pragma once

#include "render/passes/RenderPass.h"
#include "render/shadows/CSMUtil.h"
#include <functional>

namespace Nyx {

class PassShadowCSM final : public RenderPass {
public:
  void configure(uint32_t fbo, uint32_t shadowProg,
                 std::function<void(ProcMeshType)> drawFn);

  void setSettings(const CSMSettings &s) { m_settings = s; }
  const CSMSettings &settings() const { return m_settings; }

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_shadowProg = 0;
  std::function<void(ProcMeshType)> m_draw;
  CSMSettings m_settings{};
};

} // namespace Nyx
