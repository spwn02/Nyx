#include "ProjectPanel.h"

#include "platform/FileDialogs.h"
#include <imgui.h>

namespace Nyx {

void ProjectPanel::draw(ProjectManager &pm) {
  if (!ImGui::Begin("Project")) {
    ImGui::End();
    return;
  }

  if (!pm.hasProject()) {
    ImGui::TextUnformatted("No project loaded.");
    ImGui::Separator();

    if (ImGui::Button("Open .nyxproj")) {
      auto p = FileDialogs::openFile("Open Nyx Project", "nyxproj", nullptr);
      if (p)
        pm.openProjectFile(*p);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Create new project:");
    ImGui::InputText("Name", m_newName, sizeof(m_newName));
    ImGui::InputText("Root Folder (abs)", m_newRoot, sizeof(m_newRoot));

    if (ImGui::Button("Create")) {
      pm.createProjectAt(m_newRoot, m_newName);
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Recent:");
    for (const auto &r : pm.recent()) {
      if (ImGui::Selectable(r.c_str())) {
        pm.openProjectFile(r);
      }
    }

    ImGui::End();
    return;
  }

  ImGui::Text("Project: %s", pm.runtime().proj().name.c_str());
  ImGui::Text("Root: %s", pm.projectRootAbs().c_str());
  ImGui::Text("Start scene: %s", pm.runtime().proj().settings.startupScene.c_str());
  ImGui::Separator();

  ImGui::TextUnformatted("Recent:");
  for (const auto &r : pm.recent()) {
    if (ImGui::Selectable(r.c_str())) {
      pm.openProjectFile(r);
    }
  }

  ImGui::End();
}

} // namespace Nyx
