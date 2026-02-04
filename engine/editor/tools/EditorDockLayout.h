#pragma once
#include "imgui_internal.h"
#include <imgui.h>

namespace Nyx {

// Build a deterministic dock layout (fallback when imgui.ini missing/corrupt).
// Window names MUST match exactly your ImGui::Begin("...") titles.
inline void BuildDefaultDockLayout(ImGuiID dockspaceId, const ImVec2 &size) {
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, size);

  ImGuiID dockMain = dockspaceId;

  ImGuiID dockLeft = 0, dockRight = 0, dockRightDown = 0, dockBottom = 0,
          dockCenter = 0;

  dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f,
                                         nullptr, &dockMain);
  dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f,
                                          nullptr, &dockMain);
  dockRightDown = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.5f,
                                              nullptr, &dockRight);
  dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.27f,
                                           nullptr, &dockMain);
  dockCenter = dockMain;

  // Optional: split bottom into AssetBrowser + Stats
  ImGuiID dockBottomLeft = 0, dockBottomRight = 0;
  dockBottomLeft = ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Left, 0.72f,
                                               nullptr, &dockBottomRight);

  // Dock windows
  ImGui::DockBuilderDockWindow("Viewport", dockCenter);
  ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
  ImGui::DockBuilderDockWindow("Stats", dockLeft);
  ImGui::DockBuilderDockWindow("Inspector", dockRight);
  ImGui::DockBuilderDockWindow("Gizmo", dockRightDown);
  ImGui::DockBuilderDockWindow("Sky", dockRightDown);
  ImGui::DockBuilderDockWindow("Asset Browser", dockBottomLeft);

  ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace Nyx
