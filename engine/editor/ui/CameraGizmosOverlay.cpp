#include "CameraGizmosOverlay.h"

#include "scene/Camera.h"
#include "scene/EntityID.h"
#include "scene/World.h"

#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

namespace Nyx {

static glm::vec3 transformPoint(const glm::mat4 &M, const glm::vec3 &p) {
  const glm::vec4 v = M * glm::vec4(p, 1.0f);
  return glm::vec3(v) / std::max(0.000001f, v.w);
}

bool CameraGizmosOverlay::projectToScreen(const glm::mat4 &viewProj,
                                          const glm::vec3 &worldPos,
                                          const glm::vec2 &imgMin,
                                          const glm::vec2 &imgMax,
                                          glm::vec2 &outScreen) {
  const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
  if (clip.w <= 0.000001f)
    return false;

  glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (ndc.z < -1.5f || ndc.z > 1.5f)
    return false;

  const float x01 = (ndc.x * 0.5f + 0.5f);
  const float y01 = (1.0f - (ndc.y * 0.5f + 0.5f));

  const glm::vec2 size = imgMax - imgMin;
  outScreen = imgMin + glm::vec2(x01 * size.x, y01 * size.y);
  return true;
}

void CameraGizmosOverlay::drawCameraIcon(const glm::vec2 &p, float sizePx,
                                         uint32_t fillColor,
                                         uint32_t outlineColor) {
  ImDrawList *dl = ImGui::GetWindowDrawList();

  const float s = sizePx;
  const ImVec2 c(p.x, p.y);

  const ImVec2 a(c.x - s, c.y - s * 0.6f);
  const ImVec2 b(c.x + s, c.y + s * 0.6f);
  dl->AddRectFilled(a, b, fillColor, 2.0f);

  const ImVec2 l1(c.x + s, c.y - s * 0.4f);
  const ImVec2 l2(c.x + s * 1.6f, c.y);
  const ImVec2 l3(c.x + s, c.y + s * 0.4f);
  dl->AddTriangleFilled(l1, l2, l3, fillColor);

  dl->AddRect(a, b, outlineColor, 2.0f, 0, 2.0f);
  dl->AddTriangle(l1, l2, l3, outlineColor, 2.0f);
}

void CameraGizmosOverlay::draw(World &world, const glm::mat4 &editorViewProj,
                               const glm::vec2 &viewportImageMin,
                               const glm::vec2 &viewportImageMax,
                               IsEntitySelectedFn isSelected,
                               const CameraOverlaySettings &settings) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (!dl)
    return;

  for (EntityID e : world.alive()) {
    if (!world.hasCamera(e))
      continue;

    if (settings.hideEntity != InvalidEntity && settings.hideEntity == e)
      continue;

    if (settings.hideActiveCamera && world.activeCamera() == e)
      continue;

    const bool selected = isSelected ? isSelected(e) : false;
    if (!settings.showAllCameras && !selected)
      continue;

    const auto &cam = world.camera(e);
    const glm::mat4 W = world.worldTransform(e).world;

    const uint32_t fillColor =
        selected ? IM_COL32(40, 40, 40, 180) : IM_COL32(30, 30, 30, 120);
    const uint32_t lineColor =
        selected ? IM_COL32(255, 180, 60, 220) : IM_COL32(120, 120, 120, 200);

    glm::vec2 p0;
    const glm::vec3 originW = glm::vec3(W[3]);
    if (projectToScreen(editorViewProj, originW, viewportImageMin,
                        viewportImageMax, p0)) {
      drawCameraIcon(p0, settings.iconSizePx, fillColor, lineColor);
    }

    const float depth = std::max(0.01f, settings.frustumDepth);
    glm::vec3 corners[8]{};

    if (cam.projection == CameraProjection::Perspective) {
      const float fov = glm::radians(cam.fovYDeg);
      const float halfY = std::tan(fov * 0.5f) * depth;
      const float halfX = halfY;

      corners[0] = glm::vec3(-halfX, -halfY, -depth);
      corners[1] = glm::vec3(+halfX, -halfY, -depth);
      corners[2] = glm::vec3(+halfX, +halfY, -depth);
      corners[3] = glm::vec3(-halfX, +halfY, -depth);
      corners[4] = glm::vec3(0, 0, 0);
    } else {
      const float h = std::max(0.01f, cam.orthoHeight);
      const float halfY = h * 0.5f;
      const float halfX = halfY;
      const float z0 = 0.0f;
      const float z1 = -depth;

      corners[0] = glm::vec3(-halfX, -halfY, z0);
      corners[1] = glm::vec3(+halfX, -halfY, z0);
      corners[2] = glm::vec3(+halfX, +halfY, z0);
      corners[3] = glm::vec3(-halfX, +halfY, z0);

      corners[4] = glm::vec3(-halfX, -halfY, z1);
      corners[5] = glm::vec3(+halfX, -halfY, z1);
      corners[6] = glm::vec3(+halfX, +halfY, z1);
      corners[7] = glm::vec3(-halfX, +halfY, z1);
    }

    auto line = [&](const glm::vec3 &aL, const glm::vec3 &bL) {
      const glm::vec3 aW = glm::vec3(W * glm::vec4(aL, 1.0f));
      const glm::vec3 bW = glm::vec3(W * glm::vec4(bL, 1.0f));

      glm::vec2 as, bs;
      if (!projectToScreen(editorViewProj, aW, viewportImageMin,
                           viewportImageMax, as))
        return;
      if (!projectToScreen(editorViewProj, bW, viewportImageMin,
                           viewportImageMax, bs))
        return;

      dl->AddLine(ImVec2(as.x, as.y), ImVec2(bs.x, bs.y), lineColor,
                  settings.frustumLineThickness);
    };

    if (cam.projection == CameraProjection::Perspective) {
      line(corners[0], corners[1]);
      line(corners[1], corners[2]);
      line(corners[2], corners[3]);
      line(corners[3], corners[0]);

      line(corners[4], corners[0]);
      line(corners[4], corners[1]);
      line(corners[4], corners[2]);
      line(corners[4], corners[3]);
    } else {
      line(corners[0], corners[1]);
      line(corners[1], corners[2]);
      line(corners[2], corners[3]);
      line(corners[3], corners[0]);

      line(corners[4], corners[5]);
      line(corners[5], corners[6]);
      line(corners[6], corners[7]);
      line(corners[7], corners[4]);

      line(corners[0], corners[4]);
      line(corners[1], corners[5]);
      line(corners[2], corners[6]);
      line(corners[3], corners[7]);
    }
  }
}

} // namespace Nyx
