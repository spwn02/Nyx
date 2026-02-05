#include "AddMenu.h"

#include "scene/Pick.h"

#include <cstring>
#include <imgui.h>

namespace Nyx {

static bool passFilter(const char *filter, const char *item) {
  if (!filter || filter[0] == 0) return true;

  auto lower = [](char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  };

  for (const char *p = item; *p; ++p) {
    const char *a = p;
    const char *b = filter;
    while (*a && *b && lower(*a) == lower(*b)) { ++a; ++b; }
    if (*b == 0) return true;
  }
  return false;
}

void AddMenu::tick(World &world, Selection &sel, bool allowOpen) {
  ImGuiIO &io = ImGui::GetIO();

  // Blender-like: Shift+A opens Add popup
  if (allowOpen && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A, false) &&
      !io.WantTextInput) {
    std::memset(m_filter, 0, sizeof(m_filter));
    ImGui::OpenPopup("Add");
  }

  if (ImGui::BeginPopup("Add")) {
    ImGui::TextUnformatted("Add");
    ImGui::Separator();

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter",
                             "Search (e.g. cube, sphere, monkey)...",
                             m_filter, sizeof(m_filter));

    ImGui::Separator();

    struct Item {
      const char *label;
      int kind;
      ProcMeshType type;
    };
    enum : int {
      ItemMesh = 0,
      ItemCameraPersp = 100,
      ItemCameraOrtho = 101,
      ItemLightPoint = 200,
      ItemLightSpot = 201,
      ItemLightDirectional = 202,
    };
    const Item items[] = {
        {"Mesh / Cube", ItemMesh, ProcMeshType::Cube},
        {"Mesh / Plane", ItemMesh, ProcMeshType::Plane},
        {"Mesh / Circle", ItemMesh, ProcMeshType::Circle},
        {"Mesh / Sphere", ItemMesh, ProcMeshType::Sphere},
        {"Mesh / Monkey (Suzanne)", ItemMesh, ProcMeshType::Monkey},
        {"Camera / Perspective", ItemCameraPersp, ProcMeshType::Cube},
        {"Camera / Orthographic", ItemCameraOrtho, ProcMeshType::Cube},
        {"Light / Point", ItemLightPoint, ProcMeshType::Sphere},
        {"Light / Spot", ItemLightSpot, ProcMeshType::Sphere},
        {"Light / Directional", ItemLightDirectional, ProcMeshType::Sphere},
    };

    for (const auto &it : items) {
      if (!passFilter(m_filter, it.label))
        continue;

      if (ImGui::Selectable(it.label)) {
        if (it.kind == ItemMesh) {
          spawn(world, sel, it.type);
        } else if (it.kind == ItemCameraPersp ||
                   it.kind == ItemCameraOrtho) {
          EntityID e = world.createEntity(
              it.kind == ItemCameraPersp ? "Camera" : "Ortho Camera");
          auto &cam = world.ensureCamera(e);
          cam.projection = (it.kind == ItemCameraPersp)
                               ? CameraProjection::Perspective
                               : CameraProjection::Orthographic;
          cam.dirty = true;

          auto &tr = world.transform(e);
          tr.translation = {0.0f, 2.0f, 6.0f};
          tr.scale = {1.0f, 1.0f, 1.0f};
          tr.dirty = true;

          world.setActiveCamera(e);
          sel.setSinglePick(packPick(e, 0), e);
          sel.activeEntity = e;
        } else if (it.kind == ItemLightPoint || it.kind == ItemLightSpot ||
                   it.kind == ItemLightDirectional) {
          const char *name = "Point Light";
          LightType lt = LightType::Point;
          if (it.kind == ItemLightSpot) {
            name = "Spot Light";
            lt = LightType::Spot;
          } else if (it.kind == ItemLightDirectional) {
            name = "Directional Light";
            lt = LightType::Directional;
          }

          EntityID e = world.createEntity(name);
          auto &L = world.ensureLight(e);
          L.type = lt;
          L.intensity = (lt == LightType::Directional) ? 5.0f : 80.0f;
          L.radius = (lt == LightType::Directional) ? 0.0f : 8.0f;
          L.color = {1.0f, 1.0f, 1.0f};
          L.enabled = true;

          auto &mc = world.ensureMesh(e);
          if (mc.submeshes.empty())
            mc.submeshes.push_back(MeshSubmesh{});
          mc.submeshes[0].name = "Light";
          mc.submeshes[0].type = ProcMeshType::Sphere;

          auto &tr = world.transform(e);
          tr.translation = {0.0f, 2.0f, 0.0f};
          tr.scale = {0.1f, 0.1f, 0.1f};
          tr.dirty = true;

          sel.setSinglePick(packPick(e, 0), e);
          sel.activeEntity = e;
        }
        ImGui::CloseCurrentPopup();
      }
    }

    ImGui::EndPopup();
  }
}

void AddMenu::spawn(World &world, Selection &sel, ProcMeshType t) {
  const char *baseName = "Mesh";
  switch (t) {
  case ProcMeshType::Cube: baseName = "Cube"; break;
  case ProcMeshType::Plane: baseName = "Plane"; break;
  case ProcMeshType::Circle: baseName = "Circle"; break;
  case ProcMeshType::Sphere: baseName = "Sphere"; break;
  case ProcMeshType::Monkey: baseName = "Monkey"; break;
  default: break;
  }

  EntityID e = world.createEntity(baseName);

  // ensure mesh + submesh 0 exists
  auto &mc = world.ensureMesh(e);
  if (mc.submeshes.empty())
    mc.submeshes.push_back(MeshSubmesh{});

  mc.submeshes[0].name = "Submesh 0";
  mc.submeshes[0].type = t;


  auto &tr = world.transform(e);
  tr.translation = {0.0f, 0.0f, 0.0f};
  tr.scale = {1.0f, 1.0f, 1.0f};

  sel.setSinglePick(packPick(e, 0), e);
  sel.activeEntity = e;
}

} // namespace Nyx
