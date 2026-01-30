#include "render/shadows/CSMUtil.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Nyx {

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

static std::array<float, 5> computeSplits(float nearP, float farP,
                                          float lambda) {
  std::array<float, 5> d{};
  d[0] = nearP;
  for (int i = 1; i <= 4; ++i) {
    float si = float(i) / 4.0f;
    float logd = nearP * std::pow(farP / nearP, si);
    float unid = nearP + (farP - nearP) * si;
    d[i] = lerp(unid, logd, lambda);
  }
  return d;
}

static glm::mat4 makeLightView(const glm::vec3 &lightDirWS,
                               const glm::vec3 &centerWS) {
  glm::vec3 D = glm::normalize(lightDirWS);
  glm::vec3 up(0, 1, 0);
  if (std::abs(glm::dot(up, D)) > 0.95f)
    up = glm::vec3(0, 0, 1);

  glm::vec3 pos = centerWS - D * 200.0f;
  return glm::lookAt(pos, centerWS, up);
}

static void snapOrthoToTexelGrid(glm::vec3 &minLS, glm::vec3 &maxLS,
                                 uint32_t mapSize) {
  glm::vec2 ext = glm::vec2(maxLS - minLS);
  if (ext.x <= 0.0f || ext.y <= 0.0f)
    return;

  glm::vec2 texelSize = ext / float(mapSize);
  minLS.x = std::floor(minLS.x / texelSize.x) * texelSize.x;
  minLS.y = std::floor(minLS.y / texelSize.y) * texelSize.y;
  maxLS.x = minLS.x + ext.x;
  maxLS.y = minLS.y + ext.y;
}

CSMResult buildCSM(const CSMSettings &s, const glm::mat4 &camView,
                   const glm::mat4 &camProj, const glm::vec3 &lightDirWS) {
  CSMResult res{};

  auto splits = computeSplits(s.nearPlane, s.farPlane, s.lambda);

  glm::mat4 invV = glm::inverse(camView);
  glm::vec3 camPos = glm::vec3(invV[3]);
  glm::vec3 camRight = glm::normalize(glm::vec3(invV[0]));
  glm::vec3 camUp = glm::normalize(glm::vec3(invV[1]));
  glm::vec3 camFwd = -glm::normalize(glm::vec3(invV[2]));

  float tanHalfFovY = 1.0f / camProj[1][1];
  float tanHalfFovX = 1.0f / camProj[0][0];

  for (int ci = 0; ci < 4; ++ci) {
    float dn = splits[ci];
    float df = splits[ci + 1];

    res.slices[ci].splitNear = dn;
    res.slices[ci].splitFar = df;

    std::array<glm::vec3, 8> corners{};

    auto buildPlaneCorners = [&](float d, glm::vec3 *dst4) {
      float hx = d * tanHalfFovX;
      float hy = d * tanHalfFovY;
      glm::vec3 center = camPos + camFwd * d;
      dst4[0] = center - camRight * hx - camUp * hy;
      dst4[1] = center + camRight * hx - camUp * hy;
      dst4[2] = center + camRight * hx + camUp * hy;
      dst4[3] = center - camRight * hx + camUp * hy;
    };

    buildPlaneCorners(dn, &corners[0]);
    buildPlaneCorners(df, &corners[4]);

    glm::vec3 center(0.0f);
    for (auto &c : corners)
      center += c;
    center *= (1.0f / 8.0f);

    glm::mat4 lightV = makeLightView(lightDirWS, center);

    glm::vec3 minLS(1e30f), maxLS(-1e30f);
    for (auto &c : corners) {
      glm::vec4 ls = lightV * glm::vec4(c, 1.0f);
      minLS = glm::min(minLS, glm::vec3(ls));
      maxLS = glm::max(maxLS, glm::vec3(ls));
    }

    minLS.x -= s.orthoPadding;
    minLS.y -= s.orthoPadding;
    maxLS.x += s.orthoPadding;
    maxLS.y += s.orthoPadding;

    if (s.stabilize) {
      snapOrthoToTexelGrid(minLS, maxLS, s.mapSize);
    }

    float nearZ = -maxLS.z - 200.0f;
    float farZ = -minLS.z + 200.0f;

    glm::mat4 lightP =
        glm::ortho(minLS.x, maxLS.x, minLS.y, maxLS.y, nearZ, farZ);
    res.slices[ci].lightViewProj = lightP * lightV;
  }

  res.splitFar = glm::vec4(splits[1], splits[2], splits[3], splits[4]);
  return res;
}

} // namespace Nyx
