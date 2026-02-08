#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"
#include <cstdint>
#include <functional>

namespace Nyx {

class PassForwardMRT final : public RenderPass {
public:
  enum class Mode : uint8_t { Opaque = 0, Transparent = 1 };

  void setMode(Mode mode) { m_mode = mode; }
  ~PassForwardMRT() override;

  void configure(GLShaderUtil &shader, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_forwardProg = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;
  Mode m_mode = Mode::Opaque;
};

} // namespace Nyx
