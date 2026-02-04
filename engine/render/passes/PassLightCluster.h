#pragma once

#include "render/passes/RenderPass.h"
#include <cstdint>

namespace Nyx {

class GLShaderUtil;

struct LightGridMetaGPU final {
  uint32_t tileCountX = 1;
  uint32_t tileCountY = 1;
  uint32_t tileSize = 16;
  uint32_t zSlices = 16;
  uint32_t maxPerCluster = 96;
  uint32_t lightCount = 0;
  float nearZ = 0.1f;
  float farZ = 1000.0f;
  uint32_t hizMip = 0;
  uint32_t pad0 = 0;
};

class PassLightCluster final : public RenderPass {
public:
  ~PassLightCluster() override;

  void configure(GLShaderUtil &shaders);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  void setLightCount(uint32_t count) { m_lightCount = count; }

private:
  uint32_t m_prog = 0;
  uint32_t m_lightCount = 0;
};

} // namespace Nyx
