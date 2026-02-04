#pragma once

#include "scene/World.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Nyx {

struct GpuLight {
  glm::vec4 color;     // rgb=color (linear), a unused
  glm::vec4 position;  // xyz=pos, w=radius
  glm::vec4 direction; // xyz=dir, w=cosOuter
  glm::vec4 params;    // x=cosInner, y=intensity, z=type, w=castShadow
  glm::vec4 shadowData; // x=shadowMetadataIdx (packed), yzw=reserved
};

class LightSystem final {
public:
  void initGL();
  void shutdownGL();

  void updateFromWorld(const World &world);
  
  // Called after shadow passes to populate shadow metadata
  void updateShadowMetadata(const class PassShadowSpot &spotPass,
                            const class PassShadowDir &dirPass,
                            const class PassShadowPoint &pointPass);

  uint32_t ssbo() const { return m_ssbo; }
  uint32_t lightCount() const { return m_lightCount; }
  uint32_t shadowMetadataUBO() const { return m_shadowMetadataUBO; }
  bool hasPrimaryDirLight() const { return m_hasPrimaryDir; }
  uint64_t primaryDirLightKey() const { return m_primaryDirKey; }

private:
  uint32_t m_ssbo = 0;
  uint32_t m_shadowMetadataUBO = 0;
  uint32_t m_lightCount = 0;
  bool m_hasPrimaryDir = false;
  uint64_t m_primaryDirKey = 0;
  std::vector<GpuLight> m_cpuLights;
  std::vector<EntityID> m_lightEntities; // Track entity IDs for shadow metadata correlation
  
  // Shadow metadata storage
  struct SpotShadowMeta {
    glm::vec4 atlasUVMin;
    glm::vec4 atlasUVMax;
    glm::mat4 viewProj;
  };
  struct DirShadowMeta {
    glm::vec4 atlasUVMin;
    glm::vec4 atlasUVMax;
    glm::mat4 viewProj;
  };
  struct PointShadowMeta {
    glm::vec4 posAndFar; // xyz=position, w=farPlane
    uint32_t arrayIndex;
    float _pad[3];
  };
  
  std::vector<SpotShadowMeta> m_spotMetas;
  std::vector<DirShadowMeta> m_dirMetas;
  std::vector<PointShadowMeta> m_pointMetas;
};

} // namespace Nyx
