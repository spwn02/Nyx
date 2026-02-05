#include "LightSystem.h"

#include "core/Assert.h"
#include "core/Log.h"
#include "render/passes/PassShadowSpot.h"
#include "render/passes/PassShadowDir.h"
#include "render/passes/PassShadowPoint.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <unordered_map>

namespace Nyx {

void LightSystem::initGL() {
  if (m_ssbo != 0)
    return;
  glCreateBuffers(1, &m_ssbo);
  glCreateBuffers(1, &m_shadowMetadataUBO);
  const uint32_t header[4] = {0u, 0u, 0u, 0u};
  glNamedBufferData(m_ssbo, (GLsizeiptr)sizeof(header), header,
                    GL_DYNAMIC_DRAW);
  glNamedBufferData(m_shadowMetadataUBO, 4, nullptr, GL_DYNAMIC_DRAW);
}

void LightSystem::shutdownGL() {
  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
  if (m_shadowMetadataUBO) {
    glDeleteBuffers(1, &m_shadowMetadataUBO);
    m_shadowMetadataUBO = 0;
  }
}

void LightSystem::updateFromWorld(const World &world) {
  NYX_ASSERT(m_ssbo != 0, "LightSystem not initialized");
  m_cpuLights.clear();
  m_lightEntities.clear();
  m_hasPrimaryDir = false;
  m_primaryDirKey = 0;
  int primaryDirIndex = -1;
  float primaryDirIntensity = -1.0f;

  for (EntityID e : world.alive()) {
    if (!world.isAlive(e) || !world.hasLight(e))
      continue;
    const auto &L = world.light(e);
    if (!L.enabled)
      continue;

    const glm::mat4 W = world.worldTransform(e).world;
    const glm::vec3 pos = glm::vec3(W[3]);
    const glm::vec3 forward = glm::normalize(-glm::vec3(W[2]));

    GpuLight g{};
    g.color = glm::vec4(L.color, L.exposure);
    g.params.y = L.intensity;

    if (L.type == LightType::Directional) {
      g.position = glm::vec4(0, 0, 0, 0);
      g.direction = glm::vec4(forward, -1.0f); // cosOuter unused
      g.params.x = -1.0f; // cosInner unused
      g.params.z = 0.0f;  // LIGHT_DIR
    } else if (L.type == LightType::Point) {
      g.position = glm::vec4(pos, L.radius);
      g.direction = glm::vec4(0, 0, 0, 0);
      g.params.x = 0.0f; // cosInner unused
      g.params.z = 1.0f; // LIGHT_POINT
    } else { // Spot
      g.position = glm::vec4(pos, L.radius);
      const float inner = std::clamp(L.innerAngle, 0.0f, L.outerAngle);
      const float outer = std::clamp(L.outerAngle, inner, glm::pi<float>() - 1e-4f);
      const float cosOuter = std::cos(outer);
      const float cosInner = std::cos(inner);
      g.direction = glm::vec4(forward, cosOuter);
      g.params.x = cosInner;
      g.params.z = 2.0f; // LIGHT_SPOT
    }

    g.params.w = L.castShadow ? 1.0f : 0.0f;
    g.shadowData = glm::vec4(0.0f); // Will be filled by updateShadowMetadata

    if (L.type == LightType::Directional && L.castShadow) {
      const float intensity = std::max(0.0f, L.intensity);
      if (intensity > primaryDirIntensity) {
        primaryDirIntensity = intensity;
        primaryDirIndex = (int)m_cpuLights.size();
      }
    }

    m_cpuLights.push_back(g);
    m_lightEntities.push_back(e);
  }

  m_lightCount = (uint32_t)m_cpuLights.size();

  if (primaryDirIndex >= 0 &&
      primaryDirIndex < (int)m_cpuLights.size()) {
    m_cpuLights[(size_t)primaryDirIndex].shadowData.y = 1.0f;
    const EntityID e = m_lightEntities[(size_t)primaryDirIndex];
    m_primaryDirKey = (uint64_t(e.index) << 32) | uint64_t(e.generation);
    m_hasPrimaryDir = true;
  }

  struct Header {
    uint32_t count = 0;
    uint32_t pad[3] = {0u, 0u, 0u};
  } header;
  header.count = m_lightCount;

  const GLsizeiptr headerSize = (GLsizeiptr)sizeof(Header);
  const GLsizeiptr lightsSize =
      (GLsizeiptr)(m_cpuLights.size() * sizeof(GpuLight));

  glNamedBufferData(m_ssbo, headerSize + lightsSize, nullptr, GL_DYNAMIC_DRAW);
  glNamedBufferSubData(m_ssbo, 0, headerSize, &header);
  if (!m_cpuLights.empty()) {
    glNamedBufferSubData(m_ssbo, headerSize, lightsSize,
                         m_cpuLights.data());
  }
}

void LightSystem::updateShadowMetadata(const PassShadowSpot &spotPass,
                                       const PassShadowDir &dirPass,
                                       const PassShadowPoint &pointPass) {
  m_spotMetas.clear();
  m_dirMetas.clear();
  m_pointMetas.clear();

  // Build lookup maps: EntityID -> (shadow data, metadata index)
  std::unordered_map<uint64_t, uint32_t> spotIndices;
  uint32_t spotIdx = 0;
  for (const auto &spot : spotPass.getSpotLights()) {
    uint64_t key = (uint64_t(spot.entity.index) << 32) | uint64_t(spot.entity.generation);
    spotIndices[key] = spotIdx++;
  }

  std::unordered_map<uint64_t, uint32_t> dirIndices;
  uint32_t dirIdx = 0;
  for (const auto &dir : dirPass.getDirLights()) {
    uint64_t key = (uint64_t(dir.entity.index) << 32) | uint64_t(dir.entity.generation);
    dirIndices[key] = dirIdx++;
  }

  std::unordered_map<uint64_t, uint32_t> pointIndices;
  uint32_t pointIdx = 0;
  for (const auto &point : pointPass.getPointLights()) {
    uint64_t key = (uint64_t(point.entity.index) << 32) | uint64_t(point.entity.generation);
    pointIndices[key] = pointIdx++;
  }

  // Upload shadow metadata to GPU (this will be bound at binding point 10)
  // Layout: header (spot, dir, point counts), then spot metas, dir metas, point metas
  for (const auto &spot : spotPass.getSpotLights()) {
    SpotShadowMeta meta;
    float u0, v0, u1, v1;
    spot.tile.uvClamp(u0, v0, u1, v1);
    meta.atlasUVMin = glm::vec4(u0, v0, 0, 0);
    meta.atlasUVMax = glm::vec4(u1, v1, 0, 0);
    meta.viewProj = spot.viewProj;
    m_spotMetas.push_back(meta);
  }

  for (const auto &dir : dirPass.getDirLights()) {
    DirShadowMeta meta;
    float u0, v0, u1, v1;
    dir.tile.uvClamp(u0, v0, u1, v1);
    meta.atlasUVMin = glm::vec4(u0, v0, 0, 0);
    meta.atlasUVMax = glm::vec4(u1, v1, 0, 0);
    meta.viewProj = dir.viewProj;
    m_dirMetas.push_back(meta);
  }

  for (const auto &point : pointPass.getPointLights()) {
    PointShadowMeta meta;
    meta.posAndFar = glm::vec4(point.position, point.farPlane);
    meta.arrayIndex = point.arrayIndex;
    meta._pad[0] = meta._pad[1] = meta._pad[2] = 0.0f;
    m_pointMetas.push_back(meta);
  }

  struct ShadowMetaHeader {
    uint32_t spotCount;
    uint32_t dirCount;
    uint32_t pointCount;
    uint32_t _pad;
  } header;
  header.spotCount = (uint32_t)m_spotMetas.size();
  header.dirCount = (uint32_t)m_dirMetas.size();
  header.pointCount = (uint32_t)m_pointMetas.size();
  header._pad = 0u;

  const size_t headerSize = sizeof(ShadowMetaHeader);
  const size_t spotMetaSize = m_spotMetas.size() * sizeof(SpotShadowMeta);
  const size_t dirMetaSize = m_dirMetas.size() * sizeof(DirShadowMeta);
  const size_t pointMetaSize = m_pointMetas.size() * sizeof(PointShadowMeta);
  const size_t totalSize = headerSize + spotMetaSize + dirMetaSize + pointMetaSize;

  glNamedBufferData(m_shadowMetadataUBO, (GLsizeiptr)totalSize, nullptr, GL_DYNAMIC_DRAW);

  GLsizeiptr offset = 0;
  glNamedBufferSubData(m_shadowMetadataUBO, offset, (GLsizeiptr)headerSize, &header);
  offset += (GLsizeiptr)headerSize;

  if (!m_spotMetas.empty()) {
    glNamedBufferSubData(m_shadowMetadataUBO, offset, (GLsizeiptr)spotMetaSize, m_spotMetas.data());
    offset += (GLsizeiptr)spotMetaSize;
  }

  if (!m_dirMetas.empty()) {
    glNamedBufferSubData(m_shadowMetadataUBO, offset, (GLsizeiptr)dirMetaSize, m_dirMetas.data());
    offset += (GLsizeiptr)dirMetaSize;
  }

  if (!m_pointMetas.empty()) {
    glNamedBufferSubData(m_shadowMetadataUBO, offset, (GLsizeiptr)pointMetaSize, m_pointMetas.data());
  }
  
  // Now update the light SSBO with shadow metadata indices
  // This requires re-uploading the light data with updated shadowData fields
  // We need to match lights by entity ID
  if (m_lightCount > 0 && !m_cpuLights.empty() && m_cpuLights.size() == m_lightEntities.size()) {
    for (size_t i = 0; i < m_cpuLights.size(); ++i) {
      const EntityID e = m_lightEntities[i];
      const uint64_t key = (uint64_t(e.index) << 32) | uint64_t(e.generation);

      GpuLight &g = m_cpuLights[i];
      const uint32_t type = (uint32_t)(g.params.z + 0.5f);
      const bool castsShadow = (g.params.w > 0.5f);

      bool hasMeta = false;
      uint32_t metaIdx = 0u;

      if (castsShadow) {
        if (type == 2u) { // Spot
          auto it = spotIndices.find(key);
          if (it != spotIndices.end()) {
            metaIdx = it->second;
            hasMeta = true;
          }
        } else if (type == 0u) { // Directional
          // Primary dir uses CSM (no per-light meta), keep castShadow enabled.
          if (m_hasPrimaryDir && key == m_primaryDirKey) {
            hasMeta = true;
            g.shadowData.y = 1.0f;
          } else {
            auto it = dirIndices.find(key);
            if (it != dirIndices.end()) {
              metaIdx = it->second;
              hasMeta = true;
            }
          }
        } else if (type == 1u) { // Point
          auto it = pointIndices.find(key);
          if (it != pointIndices.end()) {
            metaIdx = it->second;
            hasMeta = true;
          }
        }
      }

      g.shadowData.x = hasMeta ? float(metaIdx) : 0.0f;
      if (castsShadow && !hasMeta) {
        g.params.w = 0.0f;
      }
    }

    struct Header {
      uint32_t count = 0;
      uint32_t pad[3] = {0u, 0u, 0u};
    } header;
    header.count = m_lightCount;

    const GLsizeiptr headerSize = (GLsizeiptr)sizeof(Header);
    const GLsizeiptr lightsSize =
        (GLsizeiptr)(m_cpuLights.size() * sizeof(GpuLight));

    glNamedBufferSubData(m_ssbo, 0, headerSize, &header);
    glNamedBufferSubData(m_ssbo, headerSize, lightsSize, m_cpuLights.data());
  }
}

} // namespace Nyx
