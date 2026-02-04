#pragma once

#include "RenderPass.h"
#include "../gl/GLResources.h"
#include "../gl/GLShaderUtil.h"
#include "../../scene/EntityID.h"
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace Nyx {

struct PointLightShadow {
  EntityID entity;
  uint32_t arrayIndex; // Index in cubemap array
  glm::vec3 position;
  float farPlane;
  glm::mat4 viewProj[6]; // 6 faces
};

class PassShadowPoint final : public RenderPass {
public:
  ~PassShadowPoint() override;

  void configure(GLShaderUtil &shaders, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  const std::vector<PointLightShadow>& getPointLights() const { return m_pointLights; }

private:
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;
  
  std::vector<PointLightShadow> m_pointLights;
  uint32_t m_maxPointLights = 16;
  uint16_t m_cubemapResolution = 512;
};

} // namespace Nyx
