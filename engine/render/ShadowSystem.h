#pragma once

#include "render/shadows/CSMUtil.h"
#include "scene/Components.h"
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Nyx {

class EngineContext;
class RenderableRegistry;
struct RenderPassContext;

class ShadowSystem final {
public:
  using DrawFn = std::function<void(ProcMeshType)>;

  void initGL();
  void shutdownGL();

  void render(const EngineContext &engine, const RenderableRegistry &registry,
              const RenderPassContext &ctx, const DrawFn &draw);

  uint32_t dirShadowTex() const { return m_dirShadowTex; }
  uint32_t spotShadowTex() const { return m_spotShadowTex; }
  uint32_t pointShadowTex() const { return m_pointShadowTex; }

  uint32_t dirMatricesSSBO() const { return m_dirMatricesSSBO; }
  uint32_t spotMatricesSSBO() const { return m_spotMatricesSSBO; }

  uint32_t dirCount() const { return m_dirCount; }
  uint32_t spotCount() const { return m_spotCount; }
  uint32_t pointCount() const { return m_pointCount; }

  uint32_t shadowIndex(EntityID e, LightType type) const;

  const glm::vec4 &csmSplits() const { return m_csmSplits; }
  uint32_t dirShadowSize() const { return m_dirSize; }
  uint32_t spotShadowSize() const { return m_spotSize; }
  uint32_t pointShadowSize() const { return m_pointSize; }

private:
  void ensureDirResources(uint32_t layers);
  void ensureSpotResources(uint32_t layers);
  void ensurePointResources(uint32_t layers);

  void renderDirectional(const EngineContext &engine,
                         const RenderableRegistry &registry,
                         const std::vector<EntityID> &lights,
                         const DrawFn &draw);
  void renderSpot(const EngineContext &engine,
                  const RenderableRegistry &registry,
                  const std::vector<EntityID> &lights, const DrawFn &draw);
  void renderPoint(const EngineContext &engine,
                   const RenderableRegistry &registry,
                   const std::vector<EntityID> &lights, const DrawFn &draw);

private:
  uint32_t m_dirShadowTex = 0;
  uint32_t m_spotShadowTex = 0;
  uint32_t m_pointShadowTex = 0;

  uint32_t m_dirFbo = 0;
  uint32_t m_spotFbo = 0;
  uint32_t m_pointFbo = 0;
  uint32_t m_pointDepthRbo = 0;

  uint32_t m_dirProg = 0;
  uint32_t m_pointProg = 0;

  uint32_t m_dirMatricesSSBO = 0;
  uint32_t m_spotMatricesSSBO = 0;

  uint32_t m_dirCount = 0;
  uint32_t m_spotCount = 0;
  uint32_t m_pointCount = 0;

  uint32_t m_dirLayers = 0;
  uint32_t m_spotLayers = 0;
  uint32_t m_pointLayers = 0;

  uint32_t m_dirSize = 2048;
  uint32_t m_spotSize = 1024;
  uint32_t m_pointSize = 1024;

  CSMSettings m_csmSettings{};
  glm::vec4 m_csmSplits{1.0f};

  std::unordered_map<EntityID, uint32_t, EntityHash> m_dirIndex;
  std::unordered_map<EntityID, uint32_t, EntityHash> m_spotIndex;
  std::unordered_map<EntityID, uint32_t, EntityHash> m_pointIndex;
};

} // namespace Nyx
