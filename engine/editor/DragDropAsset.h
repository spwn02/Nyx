#pragma once

#include <imgui.h>
#include <string>

namespace Nyx::DragDropAsset {

// Payload is a NUL-terminated relative path string.
static constexpr const char *PayloadRelPath = "NYX_ASSET_REL_PATH";

// Returns true if payload accepted; outRelPath filled.
inline bool acceptRelPath(std::string &outRelPath) {
  if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload(PayloadRelPath)) {
    if (p->Data && p->DataSize > 0) {
      outRelPath.assign(static_cast<const char *>(p->Data));
      return true;
    }
  }
  return false;
}

} // namespace Nyx::DragDropAsset
