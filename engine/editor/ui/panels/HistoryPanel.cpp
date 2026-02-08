#include "HistoryPanel.h"

#include "app/EngineContext.h"
#include "scene/World.h"
#include <imgui.h>

namespace Nyx {

void HistoryPanel::draw(EditorHistory &history, World &world,
                        MaterialSystem &materials, Selection &sel,
                        EngineContext &engine) {
  if (!ImGui::Begin("History")) {
    ImGui::End();
    return;
  }

  ImGui::BeginDisabled(!history.canUndo());
  if (ImGui::Button("Undo")) {
    history.undo(world, materials, sel);
    engine.rebuildRenderables();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!history.canRedo());
  if (ImGui::Button("Redo")) {
    history.redo(world, materials, sel);
    engine.rebuildRenderables();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  bool rec = history.recording();
  if (ImGui::Checkbox("Record", &rec)) {
    history.setRecording(rec);
  }

  ImGui::Separator();

  const auto &entries = history.entries();
  const int cursor = history.cursor();
  for (int i = (int)entries.size() - 1; i >= 0; --i) {
    const HistoryEntry &e = entries[(size_t)i];
    const bool active = (i == cursor);
    ImGui::PushID(e.id);
    if (ImGui::Selectable(e.label.c_str(), active)) {
      // no-op on single click
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      while (history.cursor() > i) {
        history.undo(world, materials, sel);
      }
      while (history.cursor() < i) {
        history.redo(world, materials, sel);
      }
      engine.rebuildRenderables();
    }
    if (ImGui::BeginPopupContextItem("hist_ctx")) {
      if (ImGui::MenuItem("Undo to Here")) {
        while (history.cursor() > i) {
          history.undo(world, materials, sel);
        }
        engine.rebuildRenderables();
      }
      if (ImGui::MenuItem("Redo to Here")) {
        while (history.cursor() < i) {
          history.redo(world, materials, sel);
        }
        engine.rebuildRenderables();
      }
      if (ImGui::MenuItem("Clear History")) {
        history.clear();
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
  }

  ImGui::End();
}

} // namespace Nyx
