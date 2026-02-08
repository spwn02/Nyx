#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"
#include "scene/RenderableRegistry.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Nyx {

class EngineContext;

class PassSelectionMaskTransparent final : public RenderPass {
public:
  ~PassSelectionMaskTransparent() override;

  void updateSelectedIDs(const std::vector<uint32_t> &ids);

  void configure(GLShaderUtil &shaders, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

private:
  uint32_t m_prog = 0;
  uint32_t m_fbo = 0;

  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw{};

  std::vector<uint32_t> m_selected;
};

} // namespace Nyx
