#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"
#include <functional>

namespace Nyx {

class PassMaterialPreview final : public RenderPass {
public:
  ~PassMaterialPreview() override;

  void configure(GLShaderUtil &shader, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_fbo = 0;
  uint32_t m_prog = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;
};

} // namespace Nyx
