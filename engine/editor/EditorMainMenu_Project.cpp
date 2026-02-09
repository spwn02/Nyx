#include "EditorMainMenu_Project.h"
#include "project/ProjectManager.h"
#include "scene/SceneManager.h"
#include "platform/FileDialogs.h"
#include <imgui.h>

namespace Nyx {

static bool MenuItemShortcut(const char *label, const char *shortcut) {
  return ImGui::MenuItem(label, shortcut);
}

void DrawProjectMenu(ProjectManager &pm, SceneManager *sm) {
  if (MenuItemShortcut("Save Project", "Ctrl+S")) {
    if (pm.hasProject()) {
      if (sm)
        (void)sm->saveAllProjectScenes();
      (void)pm.runtime().saveProject(pm.runtime().projectFileAbs());
    }
  }

  ImGui::Separator();

  if (MenuItemShortcut("Open Project...", "Ctrl+O")) {
    auto p = FileDialogs::openFile("Open Nyx Project", "nyxproj");
    if (p)
      pm.openProject(*p);
  }

  if (ImGui::BeginMenu("Open Recent")) {
    auto &items = pm.userCfg().recent.items;
    if (items.empty()) {
      ImGui::MenuItem("(Empty)", nullptr, false, false);
    } else {
      for (int i = 0; i < (int)items.size(); ++i) {
        const std::string &path = items[(size_t)i];
        ImGui::PushID(i);
        if (ImGui::MenuItem(path.c_str())) {
          if (!pm.openProject(path)) {
            pm.userCfg().recent.remove(path);
            pm.saveEditorConfig();
          }
        }
        ImGui::PopID();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Clear Recent")) {
        pm.userCfg().recent.clear();
        pm.saveEditorConfig();
      }
    }
    ImGui::EndMenu();
  }
}

} // namespace Nyx
