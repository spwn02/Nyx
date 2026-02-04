#include "LightGizmosOverlay.h"

#include <cmath>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

namespace Nyx {

static glm::vec3 basisForward(const glm::quat &q) {
  return q * glm::vec3(0, 0, -1);
}
static glm::vec3 basisRight(const glm::quat &q) {
  return q * glm::vec3(1, 0, 0);
}
static glm::vec3 basisUp(const glm::quat &q) { return q * glm::vec3(0, 1, 0); }

static void drawPolyline(ImDrawList *dl, const ImVec2 *pts, int count,
                         ImU32 col, float thickness) {
  if (!dl || count < 2)
    return;
  dl->AddPolyline(pts, count, col, ImDrawFlags_None, thickness);
}

static void drawCircleWorld(ImDrawList *dl, const ViewportProjector &proj,
                            const glm::vec3 &center, const glm::vec3 &axisX,
                            const glm::vec3 &axisY, float radius, ImU32 col,
                            float thickness) {
  constexpr int kSeg = 64;
  ImVec2 pts[kSeg + 1];
  int n = 0;

  for (int i = 0; i <= kSeg; ++i) {
    const float t = (float)i / (float)kSeg;
    const float a = t * 2.0f * 3.1415926535f;
    glm::vec3 p = center + (std::cos(a) * axisX + std::sin(a) * axisY) * radius;

    ImVec2 sp{};
    if (!proj.project(p, sp))
      continue;
    pts[n++] = sp;
  }

  if (n >= 2)
    drawPolyline(dl, pts, n, col, thickness);
}

void LightGizmosOverlay::draw(World &world, Selection &sel,
                              const ViewportProjector &proj) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;

  EntityID e = sel.activeEntity;
  if (e == InvalidEntity || !world.isAlive(e) || !world.hasLight(e))
    return;

  const auto &L = world.light(e);
  if (!L.enabled)
    return;

  ImDrawList *dl = ImGui::GetWindowDrawList();
  if (!dl)
    return;

  const glm::vec3 c = glm::clamp(L.color, glm::vec3(0.0f), glm::vec3(1.0f));
  const ImU32 kCol =
      IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f),
               220);
  const ImU32 kColSoft =
      IM_COL32((int)(c.x * 255.0f), (int)(c.y * 255.0f), (int)(c.z * 255.0f),
               140);

  const glm::vec3 pos = world.transform(e).translation;
  const glm::quat rot = world.transform(e).rotation;

  {
    ImVec2 sp{};
    if (proj.project(pos, sp)) {
      dl->AddCircleFilled(sp, 4.5f, kCol);
      dl->AddCircle(sp, 7.5f, kColSoft, 24, 2.0f);
    }
  }

  const glm::vec3 R = glm::normalize(basisRight(rot));
  const glm::vec3 U = glm::normalize(basisUp(rot));
  const glm::vec3 F = glm::normalize(basisForward(rot));

  if (L.type == LightType::Point) {
    const float r = (L.radius > 0.0f) ? L.radius : 0.01f;
    drawCircleWorld(dl, proj, pos, R, U, r, kColSoft, 2.0f);
    drawCircleWorld(dl, proj, pos, R, F, r, kColSoft, 2.0f);
    drawCircleWorld(dl, proj, pos, U, F, r, kColSoft, 2.0f);
  } else if (L.type == LightType::Spot) {
    const float r = (L.radius > 0.0f) ? L.radius : 0.01f;

    const float outerRad = r * std::tan(L.outerAngle);
    const float innerRad = r * std::tan(L.innerAngle);

    const glm::vec3 end = pos + F * r;
    drawCircleWorld(dl, proj, end, R, U, outerRad, kColSoft, 2.0f);
    drawCircleWorld(dl, proj, end, R, U, innerRad, kCol, 1.5f);

    const glm::vec3 edgeDirs[4] = {
        glm::normalize(F * r + R * outerRad),
        glm::normalize(F * r - R * outerRad),
        glm::normalize(F * r + U * outerRad),
        glm::normalize(F * r - U * outerRad),
    };

    for (int i = 0; i < 4; ++i) {
      ImVec2 a{}, b{};
      if (proj.project(pos, a) &&
          proj.project(pos + edgeDirs[i] * r, b)) {
        dl->AddLine(a, b, kColSoft, 2.0f);
      }
    }
  } else {
    const float len = 2.0f;
    ImVec2 a{}, b{};
    if (proj.project(pos, a) && proj.project(pos + F * len, b)) {
      dl->AddLine(a, b, kCol, 2.5f);
      dl->AddCircleFilled(b, 4.0f, kCol);
    }
  }
}

} // namespace Nyx
