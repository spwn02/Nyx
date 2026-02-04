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

struct DirLightShadow {
  EntityID entity;
  ShadowTile tile;
  glm::mat4 viewProj;
  glm::vec3 direction;
};

class PassShadowDir final : public RenderPass {
public:
  ~PassShadowDir() override;

  void configure(GLShaderUtil &shaders, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  const std::vector<DirLightShadow>& getDirLights() const { return m_dirLights; }

private:
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;
  
  DirShadowAtlasAllocator m_atlasAlloc;
  std::vector<DirLightShadow> m_dirLights;
  uint16_t m_atlasW = 2048;
  uint16_t m_atlasH = 2048;
};

} // namespace Nyx
