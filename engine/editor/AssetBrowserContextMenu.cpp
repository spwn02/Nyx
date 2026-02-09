#include "AssetBrowserContextMenu.h"

#include "assets/AssetOps.h"
#include "project/NyxProjectRuntime.h"

#include <imgui.h>

namespace Nyx {

void drawAssetBrowserContextMenu(NyxProjectRuntime &proj,
                                 const std::string &currentFolderRel,
                                 bool *outDoRescan) {
  if (outDoRescan)
    *outDoRescan = false;

  if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup))
    return;

  if (!ImGui::BeginPopupContextWindow("AB_CTX", ImGuiPopupFlags_MouseButtonRight))
    return;

  static char nameBuf[128] = "NewFolder";
  static char fileBuf[128] = "NewAsset";

  if (ImGui::MenuItem("New Folder")) {
    ImGui::OpenPopup("AB_NewFolder");
    nameBuf[0] = 0;
  }

  if (ImGui::MenuItem("New Scene (.nyxscene)")) {
    ImGui::OpenPopup("AB_NewScene");
    fileBuf[0] = 0;
  }

  if (ImGui::MenuItem("New NAsset (.nasset)")) {
    ImGui::OpenPopup("AB_NewNAsset");
    fileBuf[0] = 0;
  }

  if (ImGui::BeginPopupModal("AB_NewFolder", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Folder name", nameBuf, sizeof(nameBuf));
    if (ImGui::Button("Create")) {
      std::string rel = currentFolderRel;
      if (!rel.empty() && rel.back() != '/')
        rel += "/";
      rel += nameBuf;

      if (AssetOps::createFolder(proj, rel)) {
        if (outDoRescan)
          *outDoRescan = true;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("AB_NewScene", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Scene name", fileBuf, sizeof(fileBuf));
    ImGui::TextUnformatted("Creates an empty .nyxscene placeholder.");
    if (ImGui::Button("Create")) {
      std::string rel = currentFolderRel;
      if (!rel.empty() && rel.back() != '/')
        rel += "/";
      rel += fileBuf;
      if (!rel.ends_with(".nyxscene"))
        rel += ".nyxscene";

      if (AssetOps::createEmptyTextFile(proj, rel, "")) {
        if (outDoRescan)
          *outDoRescan = true;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("AB_NewNAsset", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::InputText("Asset name", fileBuf, sizeof(fileBuf));
    ImGui::TextUnformatted("Use for animation/post/material graphs.");
    if (ImGui::Button("Create")) {
      std::string rel = currentFolderRel;
      if (!rel.empty() && rel.back() != '/')
        rel += "/";
      rel += fileBuf;
      if (!rel.ends_with(".nasset"))
        rel += ".nasset";

      if (AssetOps::createEmptyTextFile(proj, rel, "")) {
        if (outDoRescan)
          *outDoRescan = true;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::EndPopup();
}

} // namespace Nyx
