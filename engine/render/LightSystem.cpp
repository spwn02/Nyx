#include "LightSystem.h"

#include "core/Assert.h"
#include "render/ShadowSystem.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>

namespace Nyx {

void LightSystem::initGL() {
  if (m_ssbo != 0)
    return;
  glCreateBuffers(1, &m_ssbo);
  const uint32_t header[4] = {0u, 0u, 0u, 0u};
  glNamedBufferData(m_ssbo, (GLsizeiptr)sizeof(header), header,
                    GL_DYNAMIC_DRAW);
}

void LightSystem::shutdownGL() {
  if (m_ssbo) {
    glDeleteBuffers(1, &m_ssbo);
    m_ssbo = 0;
  }
}

void LightSystem::updateFromWorld(const World &world,
                                  const ShadowSystem *shadows) {
  NYX_ASSERT(m_ssbo != 0, "LightSystem not initialized");
  m_cpuLights.clear();

  for (EntityID e : world.alive()) {
    if (!world.isAlive(e) || !world.hasLight(e))
      continue;
    const auto &L = world.light(e);
    if (!L.enabled)
      continue;

    GpuLight g{};

    const glm::mat4 W = world.worldTransform(e).world;
    const glm::vec3 pos = glm::vec3(W[3]);
    const glm::vec3 forward = glm::normalize(-glm::vec3(W[2]));

    const uint32_t type =
        (L.type == LightType::Directional)
            ? 2u
            : (L.type == LightType::Spot ? 1u : 0u);

    g.posRadius = glm::vec4(pos, L.radius);
    g.dirType = glm::vec4(forward, (float)type);
    g.colorIntensity = glm::vec4(L.color, L.intensity);

    const float inner = std::clamp(L.innerAngle, 0.0f, L.outerAngle);
    const float outer = std::clamp(L.outerAngle, inner, glm::pi<float>() - 1e-4f);
    g.spotParams = glm::vec4(std::cos(inner), std::cos(outer), 0.0f, 0.0f);

    const uint32_t shadowIndex =
        shadows ? shadows->shadowIndex(e, L.type) : 0xFFFFFFFFu;
    g.shadow = glm::uvec4(shadowIndex, 0u, 0u, 0u);

    m_cpuLights.push_back(g);
  }

  m_lightCount = (uint32_t)m_cpuLights.size();

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

} // namespace Nyx
