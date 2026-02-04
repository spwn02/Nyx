#pragma once

#include "RenderPass.h"
#include "../light/ShadowAtlasAllocator.h"
#include "../gl/GLResources.h"
#include "../gl/GLShaderUtil.h"
#include "../../scene/EntityID.h"
#include <vector>
#include <functional>
#include <glm/glm.hpp>

namespace Nyx {

struct SpotLightShadow {
  EntityID entity;
  ShadowTile tile;
  glm::mat4 viewProj;
  glm::vec3 position;
  glm::vec3 direction;
  float innerAngle;
  float outerAngle;
};

class PassShadowSpot final : public RenderPass {
public:
  ~PassShadowSpot() override;

  void configure(GLShaderUtil &shaders, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  const std::vector<SpotLightShadow>& getSpotLights() const { return m_spotLights; }

private:
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;
  
  SpotShadowAtlasAllocator m_atlasAlloc;
  std::vector<SpotLightShadow> m_spotLights;
  uint16_t m_atlasW = 2048;
  uint16_t m_atlasH = 2048;
};

} // namespace Nyx
