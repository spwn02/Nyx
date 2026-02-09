#include "ProjectBrowserPanel.h"
#include "core/Assert.h"
#include "core/Log.h"
#include "platform/FileDialogs.h" // Nyx::FileDialogs::openFile

#include <filesystem>

#include <imgui.h>

namespace Nyx {

static void drawRecent(ProjectManager &pm) {
  auto &rec = pm.userCfg().recent;

  ImGui::TextUnformatted("Recent Projects");
  ImGui::Separator();

  if (rec.items.empty()) {
    ImGui::TextDisabled("No recent projects.");
    return;
  }

  for (int i = 0; i < (int)rec.items.size(); ++i) {
    const std::string &path = rec.items[(size_t)i];

    ImGui::PushID(i);
    if (ImGui::Selectable(path.c_str(), false)) {
      if (!pm.openProject(path)) {
        // If missing, remove it to keep list clean.
        pm.userCfg().recent.remove(path);
        pm.saveEditorConfig();
      }
    }
    if (ImGui::BeginPopupContextItem("recent_ctx")) {
      if (ImGui::MenuItem("Remove from list")) {
        pm.userCfg().recent.remove(path);
        pm.saveEditorConfig();
        ImGui::EndPopup();
        ImGui::PopID();
        break;
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }
}

void ProjectBrowserPanel::draw(ProjectManager &pm) {
  if (m_open) {
    ImGui::OpenPopup("Project Browser");
    m_open = false;
    m_closeBrowserNextFrame = false;
  }

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);

  if (!ImGui::BeginPopupModal("Project Browser", nullptr,
                              ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoDocking)) {
    return;
  }

  // Left: recent list
  ImGui::BeginChild("##left", ImVec2(0, 0), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_None);
  if (ImGui::Button("Open .nyxproj...")) {
    auto p = FileDialogs::openFile("Open Nyx Project", "nyxproj");
    if (p && pm.openProject(*p))
      m_closeBrowserNextFrame = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("Create New...")) {
    m_createError.clear();

    auto p = FileDialogs::saveFile("Create Nyx Project", "nyxproj",
                                   "NyxProject.nyxproj");
    if (p) {
      std::filesystem::path chosen(*p);
      if (chosen.extension() != ".nyxproj")
        chosen += ".nyxproj";
      const std::string abs = chosen.lexically_normal().string();
      const std::string name = chosen.stem().string();
      if (pm.createProject(abs, name, true)) {
        m_closeBrowserNextFrame = true;
      } else {
        m_createError =
            "Failed to create/open project. Check path and permissions.";
      }
    }
  }

  ImGui::Spacing();
  drawRecent(pm);
  ImGui::EndChild();

  if (!m_createError.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
                       m_createError.c_str());
  }

  if (m_closeBrowserNextFrame) {
    m_closeBrowserNextFrame = false;
    ImGui::CloseCurrentPopup(); // close "Project Browser"
  }

  ImGui::EndPopup();
}

} // namespace Nyx
