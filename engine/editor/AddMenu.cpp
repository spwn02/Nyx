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

    struct Item { const char *label; ProcMeshType type; };
    const Item items[] = {
        {"Mesh / Cube", ProcMeshType::Cube},
        {"Mesh / Plane", ProcMeshType::Plane},
        {"Mesh / Circle", ProcMeshType::Circle},
        {"Mesh / Sphere", ProcMeshType::Sphere},
        {"Mesh / Monkey (Suzanne)", ProcMeshType::Monkey},
    };

    for (const auto &it : items) {
      if (!passFilter(m_filter, it.label)) continue;

      if (ImGui::Selectable(it.label)) {
        spawn(world, sel, it.type);
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
  auto &mc = world.ensureMesh(e, t, 1);
  if (mc.submeshes.empty())
    mc.submeshes.push_back(MeshSubmesh{});

  mc.submeshes[0].name = "Submesh 0";
  // mc.submeshes[0].type = t;

  // ensure material exists for submesh0
  (void)world.materialHandle(e, 0);

  auto &tr = world.transform(e);
  tr.translation = {0.0f, 0.0f, 0.0f};
  tr.scale = {1.0f, 1.0f, 1.0f};

  sel.setSinglePick(packPick(e, 0));
  sel.activeEntity = e;
}

} // namespace Nyx