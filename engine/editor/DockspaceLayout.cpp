#include "DockspaceLayout.h"

#include "editor/EditorDockLayout.h"

#include <imgui.h>

namespace Nyx {

static bool iniLoaded() {
  ImGuiContext *g = ImGui::GetCurrentContext();
  if (!g)
    return false;

  ImGuiIO &io = ImGui::GetIO();
  if (!io.IniFilename)
    return false;

  return g->SettingsLoaded;
}

void DockspaceLayout::applyDefaultLayoutIfNeeded(EditorState &st,
                                                 unsigned dockspaceId) {
  if (iniLoaded()) {
    st.dockFallbackApplied = true;
    return;
  }

  if (st.dockFallbackApplied)
    return;

  st.dockFallbackApplied = true;

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const ImVec2 size = vp ? vp->WorkSize : ImVec2(1280, 720);
  BuildDefaultDockLayout(dockspaceId, size);
}

} // namespace Nyx
