#pragma once

#include "render/gl/GLResources.h"
#include "render/gl/GLShaderUtil.h"
#include "render/passes/RenderPass.h"
#include "render/light/ShadowAtlasAllocator.h"
#include <functional>
#include <glm/glm.hpp>

namespace Nyx {

struct ShadowCSMUBO final {
  glm::mat4 lightViewProj[4] = {glm::mat4(1.0f), glm::mat4(1.0f),
                               glm::mat4(1.0f), glm::mat4(1.0f)};
  glm::vec4 splitDepths = glm::vec4(1, 1, 1, 1);
  glm::vec4 shadowMapSize = glm::vec4(2048, 2048, 1.0f / 2048.0f,
                                      1.0f / 2048.0f);
  glm::vec4 biasParams = glm::vec4(0.003f, 0.0005f, 0.002f, 0.0f);
  glm::vec4 misc = glm::vec4(4.0f, 0.01f, 200.0f, 0.0f);
  glm::vec4 lightDir = glm::vec4(0, -1, 0, 0);
  
  // Atlas UV transforms: [0]=min, [1]=max for each cascade
  glm::vec4 atlasUVMin[4] = {glm::vec4(0), glm::vec4(0), glm::vec4(0), glm::vec4(0)};
  glm::vec4 atlasUVMax[4] = {glm::vec4(1), glm::vec4(1), glm::vec4(1), glm::vec4(1)};
};

struct ShadowCSMConfig final {
  uint32_t cascadeCount = 4;
  uint32_t shadowRes = 2048;
  float splitLambda = 0.65f;
  float lightDirYawDeg = 45.0f;
  float lightDirPitchDeg = -60.0f;
  float csmNear = 0.05f;
  float csmFar = 200.0f;
  float lightViewDistance = 250.0f;
  float aabbPadding = 5.0f;
  float zPadding = 50.0f;
  float rasterSlopeScale = 2.0f;
  float rasterConstant = 1.0f;
  float normalBias = 0.003f;
  float receiverBias = 0.0005f;
  float slopeBias = 0.002f;
  bool cullFrontFaces = false;
  bool stabilize = true;
};

class PassShadowCSM final : public RenderPass {
public:
  ~PassShadowCSM() override;

  void configure(GLShaderUtil &shaders, GLResources &res,
                 std::function<void(ProcMeshType)> drawFn);

  void setup(RenderGraph &graph, const RenderPassContext &ctx,
             const RenderableRegistry &registry, EngineContext &engine,
             bool editorVisible) override;

  ShadowCSMConfig &config() { return m_cfg; }
  const ShadowCSMConfig &config() const { return m_cfg; }

private:
  uint32_t m_fbo = 0;
  GLResources *m_res = nullptr;
  std::function<void(ProcMeshType)> m_draw;

  ShadowCSMConfig m_cfg{};
  ShadowCSMUBO m_uboCPU{};

  // Shadow atlas (4096x4096 packing 4 cascades)
  ShadowAtlasAllocator m_atlasAlloc;
  ShadowTile m_cascadeTiles[4];
  bool m_useAtlas = true;
};

} // namespace Nyx
