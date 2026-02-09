#include "SceneBrowserPanel.h"
#include "platform/FileDialogs.h"
#include <filesystem>
#include <imgui.h>

namespace Nyx {

void SceneBrowserPanel::queueOpen(const std::string &absPath) {
  m_pendingAction = PendingAction::OpenScene;
  m_pendingPathAbs = absPath;
}

void SceneBrowserPanel::queueCreate(const std::string &absPath) {
  m_pendingAction = PendingAction::NewScene;
  m_pendingPathAbs = absPath;
}

bool SceneBrowserPanel::executePending(SceneManager &sm) {
  if (m_pendingAction == PendingAction::None || m_pendingPathAbs.empty())
    return false;

  bool ok = false;
  switch (m_pendingAction) {
  case PendingAction::OpenScene:
    ok = sm.openScene(m_pendingPathAbs);
    if (!ok)
      m_lastError = "Failed to open scene: " + m_pendingPathAbs;
    break;
  case PendingAction::NewScene:
    ok = sm.createScene(m_pendingPathAbs);
    if (!ok)
      m_lastError = "Failed to create scene: " + m_pendingPathAbs;
    break;
  case PendingAction::None:
    break;
  }

  m_pendingAction = PendingAction::None;
  m_pendingPathAbs.clear();
  return ok;
}

void SceneBrowserPanel::draw(SceneManager &sm, ProjectManager &pm) {
  if (!pm.hasProject())
    return;

  ImGuiWindowFlags wndFlags = ImGuiWindowFlags_None;
  if (sm.hasActive() && sm.active().dirty)
    wndFlags |= ImGuiWindowFlags_UnsavedDocument;
  ImGui::Begin("Scenes", nullptr, wndFlags);

  for (const auto &entry : pm.runtime().proj().scenes) {
    ImGui::PushID(entry.relPath.c_str());
    if (ImGui::Selectable(entry.relPath.c_str())) {
      const std::string abs = pm.runtime().makeAbsolute(entry.relPath);
      if (sm.hasActive() && sm.active().dirty) {
        queueOpen(abs);
        ImGui::OpenPopup("Unsaved Scene");
      } else {
        sm.openScene(abs);
      }
    }
    ImGui::PopID();
  }

  ImGui::Separator();

  if (ImGui::Button("New Scene")) {
    const std::string defaultNewSceneAbs =
        pm.runtime().makeAbsolute("Content/Scenes/NewScene.nyxscene");
    auto p = FileDialogs::saveFile("Create Scene", "nyxscene",
                                   defaultNewSceneAbs.c_str());
    if (p) {
      std::filesystem::path chosen(*p);
      if (chosen.extension() != ".nyxscene")
        chosen += ".nyxscene";
      const std::string abs = chosen.lexically_normal().string();
      if (sm.hasActive() && sm.active().dirty) {
        queueCreate(abs);
        ImGui::OpenPopup("Unsaved Scene");
      } else {
        if (!sm.createScene(abs))
          m_lastError = "Failed to create scene: " + abs;
      }
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Open Scene")) {
    auto p = FileDialogs::openFile("Open Scene", "nyxscene");
    if (p) {
      if (sm.hasActive() && sm.active().dirty) {
        queueOpen(*p);
        ImGui::OpenPopup("Unsaved Scene");
      } else {
        if (!sm.openScene(*p))
          m_lastError = "Failed to open scene: " + *p;
      }
    }
  }

  if (sm.hasActive()) {
    ImGui::Separator();
    if (ImGui::Button("Save")) {
      if (!sm.saveActive())
        m_lastError = "Failed to save active scene.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As")) {
      auto p = FileDialogs::saveFile("Save Scene As", "nyxscene",
                                     sm.active().pathAbs.c_str());
      if (p && !sm.saveActiveAs(*p))
        m_lastError = "Failed to save scene as: " + *p;
    }
  }

  if (!m_lastError.empty()) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
                       m_lastError.c_str());
  }

  if (ImGui::BeginPopupModal("Unsaved Scene", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Current scene has unsaved changes.");
    ImGui::TextUnformatted("Save before continuing?");

    if (ImGui::Button("Save")) {
      if (!sm.hasActive() || sm.saveActive()) {
        executePending(sm);
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save")) {
      executePending(sm);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      m_pendingAction = PendingAction::None;
      m_pendingPathAbs.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}

} // namespace Nyx
