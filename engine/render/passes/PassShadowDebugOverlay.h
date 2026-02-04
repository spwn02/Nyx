#pragma once

#include "render/ShadowDebugMode.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"

namespace Nyx {

class PassShadowDebugOverlay final : public RenderPass {
public:
  ~PassShadowDebugOverlay() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  void setMode(ShadowDebugMode mode) { m_mode = mode; }
  ShadowDebugMode mode() const { return m_mode; }

  void setOverlayAlpha(float alpha) { m_alpha = alpha; }
  float overlayAlpha() const { return m_alpha; }

private:
  ShadowDebugMode m_mode = ShadowDebugMode::None;
  float m_alpha = 0.85f;
};

} // namespace Nyx
