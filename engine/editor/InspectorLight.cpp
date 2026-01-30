#include "editor/InspectorLight.h"

#include "scene/WorldEvents.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Nyx {

bool InspectorLight::draw(World &world, Selection &sel) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return false;

  EntityID e = sel.activeEntity;
  if (e == InvalidEntity)
    return false;

  if (e == InvalidEntity || !world.isAlive(e) || !world.hasLight(e))
    return false;

  bool changed = false;
  auto &L = world.light(e);

  ImGui::SeparatorText("Light");

  bool enabled = L.enabled;
  if (ImGui::Checkbox("Enabled", &enabled)) {
    L.enabled = enabled;
    changed = true;
  }

  {
    int cur = (int)L.type;
    const char *items[] = {"Directional", "Point", "Spot"};
    if (ImGui::Combo("Type", &cur, items, 3)) {
      L.type = (LightType)cur;
      changed = true;
    }
  }

  {
    float col[3] = {L.color.x, L.color.y, L.color.z};
    if (ImGui::ColorEdit3("Color", col, ImGuiColorEditFlags_Float)) {
      L.color = {col[0], col[1], col[2]};
      changed = true;
    }
  }

  if (ImGui::DragFloat("Intensity", &L.intensity, 0.5f, 0.0f, 500000.0f,
                       "%.3f")) {
    L.intensity = std::max(0.0f, L.intensity);
    changed = true;
  }

  if (L.type == LightType::Point || L.type == LightType::Spot) {
    if (ImGui::DragFloat("Range", &L.radius, 0.05f, 0.01f, 100000.0f,
                         "%.3f")) {
      L.radius = std::max(0.01f, L.radius);
      changed = true;
    }
  }

  if (L.type == LightType::Spot) {
    float inner = glm::degrees(L.innerAngle);
    float outer = glm::degrees(L.outerAngle);
    if (ImGui::DragFloat("Inner Angle (deg)", &inner, 0.1f, 0.0f, 179.0f,
                         "%.2f")) {
      inner = std::clamp(inner, 0.0f, 179.0f);
      if (outer < inner)
        outer = inner;
      L.innerAngle = glm::radians(inner);
      L.outerAngle = glm::radians(outer);
      changed = true;
    }
    if (ImGui::DragFloat("Outer Angle (deg)", &outer, 0.1f, 0.0f, 179.0f,
                         "%.2f")) {
      outer = std::clamp(outer, 0.0f, 179.0f);
      if (outer < inner)
        inner = outer;
      L.innerAngle = glm::radians(inner);
      L.outerAngle = glm::radians(outer);
      changed = true;
    }
  }

  if (changed) {
    world.events().push({WorldEventType::LightChanged, e});
  }

  return changed;
}

} // namespace Nyx
