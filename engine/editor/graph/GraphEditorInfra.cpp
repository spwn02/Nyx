#include "editor/graph/GraphEditorInfra.h"

#include <imgui_node_editor.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <string>

namespace ed = ax::NodeEditor;

namespace Nyx::GraphEditorInfra {

bool passFilterCI(const char *filter, const char *text) {
  if (!filter || filter[0] == 0)
    return true;
  auto lower = [](char c) -> char {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  };
  for (const char *p = text; *p; ++p) {
    const char *a = p;
    const char *b = filter;
    while (*a && *b && lower(*a) == lower(*b)) {
      ++a;
      ++b;
    }
    if (*b == 0)
      return true;
  }
  return false;
}

std::string filenameOnly(const std::string &path) {
  if (path.empty())
    return {};
  const size_t pos = path.find_last_of("/\\");
  return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

bool hasExtensionCI(const std::string &path, const char *extNoDot) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || !extNoDot || !extNoDot[0])
    return false;
  std::string ext = path.substr(dot + 1);
  std::string want = extNoDot;
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  std::transform(want.begin(), want.end(), want.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext == want;
}

void triggerAddMenuAtMouse(bool panelHovered, PopupState &state, char *searchBuf,
                           size_t searchBufSize) {
  if (!panelHovered)
    return;
  if (!(ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_A)))
    return;
  state.open = true;
  state.requestOpen = true;
  state.popupPos = ImGui::GetMousePos();
  if (searchBuf && searchBufSize > 0)
    std::snprintf(searchBuf, searchBufSize, "%s", "");
}

void preparePopupOpen(const char *popupName, PopupState &state) {
  ImGui::SetNextWindowPos(state.popupPos, ImGuiCond_Appearing);
  if (state.requestOpen) {
    ImGui::OpenPopup(popupName);
    state.requestOpen = false;
  }
}

bool drawPalettePopup(const char *popupName, const char *title,
                      const char *searchHint, PopupState &state, char *searchBuf,
                      size_t searchBufSize, IGraphAdapter &adapter) {
  preparePopupOpen(popupName, state);
  if (!ImGui::BeginPopup(popupName, ImGuiWindowFlags_AlwaysAutoResize))
    return false;

  const char *popupTitle = title ? title : "Add";
  ImGui::TextUnformatted(popupTitle);
  ImGui::Separator();

  ImGui::SetNextItemWidth(260.0f);
  const char *hint = searchHint ? searchHint : "Search...";
  ImGui::InputTextWithHint("##search", hint, searchBuf, searchBufSize);
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere(-1);
  ImGui::Separator();

  const std::vector<PaletteItem> &items = adapter.paletteItems();
  const std::vector<const char *> &cats = adapter.paletteCategories();
  const bool filterActive = searchBuf && searchBuf[0] != 0;

  bool added = false;
  for (const char *cat : cats) {
    bool anyInCat = false;
    for (const PaletteItem &it : items) {
      if (std::strcmp(it.category, cat) != 0)
        continue;
      if (!passFilterCI(searchBuf, it.name))
        continue;
      anyInCat = true;
      break;
    }
    if (!anyInCat)
      continue;

    if (filterActive)
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!ImGui::TreeNode(cat))
      continue;

    for (const PaletteItem &it : items) {
      if (std::strcmp(it.category, cat) != 0)
        continue;
      if (!passFilterCI(searchBuf, it.name))
        continue;
      if (!ImGui::Selectable(it.name))
        continue;
      added = adapter.addPaletteItem(it.id, state.popupPos);
      if (added) {
        ImGui::CloseCurrentPopup();
        state.open = false;
      }
      break;
    }

    ImGui::TreePop();
    if (added)
      break;
  }

  ImGui::EndPopup();
  return added;
}

bool graphWindowWantsPriority() {
  return ImGui::IsWindowHovered(
             ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
             ImGuiHoveredFlags_ChildWindows) ||
         ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
}

ax::NodeEditor::EditorContext *createNodeEditorContext(const char *settingsFile) {
  ed::Config cfg{};
  cfg.SettingsFile = settingsFile;
  return ed::CreateEditor(&cfg);
}

void destroyNodeEditorContext(ax::NodeEditor::EditorContext *&ctx) {
  if (!ctx)
    return;
  ed::DestroyEditor(ctx);
  ctx = nullptr;
}

} // namespace Nyx::GraphEditorInfra
