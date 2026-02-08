#include "ProjectSettingsPanel.h"

#include "app/EngineContext.h"
#include "editor/EditorLayer.h"
#include <imgui.h>
#include <algorithm>

namespace Nyx {

void ProjectSettingsPanel::draw(EditorLayer &editor, EngineContext &engine) {
  if (!ImGui::Begin("Project Settings")) {
    ImGui::End();
    return;
  }

  float fps = editor.projectFps();
  if (fps <= 0.0f)
    fps = 30.0f;
  if (ImGui::InputFloat("FPS", &fps, 0.0f, 0.0f, "%.2f")) {
    fps = std::max(1.0f, fps);
    editor.setProjectFps(fps);
    engine.animation().setFps(fps);
  }

  ImGui::End();
}

} // namespace Nyx
