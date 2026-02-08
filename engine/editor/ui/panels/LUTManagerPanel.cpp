#include "LUTManagerPanel.h"

#include "app/EngineContext.h"
#include "platform/FileDialogs.h"
#include "editor/ui/UiPayloads.h"
#include <imgui.h>

namespace Nyx {

static const char *filenameOnly(const std::string &path) {
  if (path.empty())
    return "Identity";
  const size_t a = path.find_last_of("/\\");
  return (a == std::string::npos) ? path.c_str() : path.c_str() + a + 1;
}

void LUTManagerPanel::draw(EngineContext &engine) {
  ImGui::Begin("LUT Manager");

  if (ImGui::Button("Load...")) {
    if (auto path = FileDialogs::openFile("Load LUT", "cube", nullptr)) {
      if (!path->empty())
        engine.ensurePostLUT3D(*path);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Reload")) {
    const auto &paths = engine.postLUTPaths();
    if (m_selectedIndex > 0 && m_selectedIndex < (int)paths.size()) {
      engine.reloadPostLUT3D(paths[(size_t)m_selectedIndex]);
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    if (m_selectedIndex > 0) {
      engine.clearPostLUT(m_selectedIndex);
      m_selectedIndex = 0;
    }
  }

  ImGui::Separator();

  const auto &paths = engine.postLUTPaths();
  if (ImGui::BeginListBox("##lut_list", ImVec2(-1.0f, 0.0f))) {
    for (size_t i = 0; i < paths.size(); ++i) {
      const bool selected = (int)i == m_selectedIndex;
      const uint32_t size = engine.postLUTSize((uint32_t)i);
      const std::string label =
          std::to_string(i) + "  " + filenameOnly(paths[i]) + "  (" +
          std::to_string(size) + ")";
      if (ImGui::Selectable(label.c_str(), selected)) {
        m_selectedIndex = (int)i;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndListBox();
  }

  // Drag/drop .cube from Asset Browser
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload =
            ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
      const char *p = (const char *)payload->Data;
      if (p) {
        std::string path(p);
        engine.ensurePostLUT3D(path);
      }
    }
    ImGui::EndDragDropTarget();
  }

  ImGui::End();
}

} // namespace Nyx
