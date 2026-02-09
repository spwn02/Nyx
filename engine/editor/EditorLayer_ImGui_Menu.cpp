#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "editor/EditorMainMenu_Project.h"
#include "platform/FileDialogs.h"
#include "tools/EditorDockLayout.h"

#include <cstdio>
#include <filesystem>

#include <imgui.h>

namespace Nyx {

namespace {

void enableDefaultWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.viewport = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.sky = true;
  panels.assetBrowser = true;
  panels.stats = true;
}

void enableMaterialWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.materialGraph = true;
  panels.lutManager = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.assetBrowser = true;
  panels.sky = true;
}

void enablePostProcessingWorkspacePanels(EditorPanels &panels) {
  panels = {};
  panels.postGraph = true;
  panels.hierarchy = true;
  panels.inspector = true;
  panels.assetBrowser = true;
}

} // namespace

void EditorLayer::drawMainMenuBar(EngineContext &engine) {
  if (!ImGui::BeginMenuBar())
    return;

  if (ImGui::BeginMenu("File")) {
    if (m_projectManager) {
      DrawProjectMenu(*m_projectManager, m_sceneManager);
      ImGui::Separator();
    }

    if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
      if (m_sceneManager) {
        const char *defaultNewScene = "Main.nyxscene";
        std::string defaultNewSceneAbs;
        if (m_projectManager && m_projectManager->hasProject()) {
          defaultNewSceneAbs =
              m_projectManager->runtime().makeAbsolute("Content/Scenes/NewScene.nyxscene");
          defaultNewScene = defaultNewSceneAbs.c_str();
        }
        auto p = FileDialogs::saveFile("Create Scene", "nyxscene", defaultNewScene);
        if (p) {
          std::filesystem::path chosen(*p);
          if (chosen.extension() != ".nyxscene")
            chosen += ".nyxscene";
          if (m_sceneManager->createScene(chosen.lexically_normal().string())) {
            m_scenePath = m_sceneManager->active().pathAbs;
            m_sceneLoaded = true;
            m_lastAutoSaveSerial = engine.materials().changeSerial();
            m_sel.clear();
            m_hierarchy.setWorld(m_world);
            engine.rebuildEntityIndexMap();
            engine.rebuildRenderables();
          }
        }
      } else {
        defaultScene(engine);
      }
    }

    if (ImGui::MenuItem("Open Scene...", "Ctrl+Shift+O")) {
      m_openScenePopup = true;
      std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", m_scenePath.c_str());
    }

    if (ImGui::MenuItem("Save Scene")) {
      if (m_sceneManager && m_sceneManager->hasActive()) {
        if (!m_sceneManager->saveActive()) {
          Log::Warn("Failed to save scene to {}", m_scenePath);
        } else {
          m_scenePath = m_sceneManager->active().pathAbs;
          markSceneClean(engine);
        }
      } else {
        m_saveScenePopup = true;
        std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", m_scenePath.c_str());
      }
    }

    if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
      m_saveScenePopup = true;
      std::snprintf(m_scenePathBuf, sizeof(m_scenePathBuf), "%s", m_scenePath.c_str());
    }

    ImGui::Separator();
    ImGui::MenuItem("Auto Save", "Ctrl+Alt+S", &m_autoSave);
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Window")) {
    if (ImGui::BeginMenu("Workspaces")) {
      if (ImGui::MenuItem("Default")) {
        m_persist.dockLayoutApplied = false;
        enableDefaultWorkspacePanels(m_persist.panels);
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        BuildDefaultDockLayout(engine.dockspaceID(), vp->WorkSize);
      }
      if (ImGui::MenuItem("Material Editing")) {
        m_persist.dockLayoutApplied = false;
        enableMaterialWorkspacePanels(m_persist.panels);
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        BuildMaterialEditingDockLayout(engine.dockspaceID(), vp->WorkSize);
      }
      if (ImGui::MenuItem("Post-Processing Editing")) {
        m_persist.dockLayoutApplied = false;
        enablePostProcessingWorkspacePanels(m_persist.panels);
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        BuildPostProcessingEditingDockLayout(engine.dockspaceID(), vp->WorkSize);
      }
      ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Reset Layout")) {
      m_persist.dockLayoutApplied = false;
      enableDefaultWorkspacePanels(m_persist.panels);
      const ImGuiViewport *vp = ImGui::GetMainViewport();
      BuildDefaultDockLayout(engine.dockspaceID(), vp->WorkSize);
    }

    ImGui::MenuItem("Viewport", nullptr, &m_persist.panels.viewport);
    ImGui::MenuItem("Hierarchy", nullptr, &m_persist.panels.hierarchy);
    ImGui::MenuItem("Inspector", nullptr, &m_persist.panels.inspector);
    ImGui::MenuItem("Sky", nullptr, &m_persist.panels.sky);
    ImGui::MenuItem("Stats", nullptr, &m_persist.panels.stats);
    ImGui::MenuItem("Project Settings", nullptr, &m_persist.panels.projectSettings);
    ImGui::MenuItem("Asset Browser", nullptr, &m_persist.panels.assetBrowser);
    ImGui::MenuItem("LUT Manager", nullptr, &m_persist.panels.lutManager);
    ImGui::MenuItem("Material Graph", nullptr, &m_persist.panels.materialGraph);
    ImGui::MenuItem("Post-Processing Graph", nullptr, &m_persist.panels.postGraph);
    ImGui::MenuItem("Sequencer", nullptr, &m_persist.panels.sequencer);
    ImGui::MenuItem("History", nullptr, &m_persist.panels.history);
    ImGui::EndMenu();
  }

  ImGui::EndMenuBar();
}

void EditorLayer::drawSceneFilePopups(EngineContext &engine) {
  if (m_openScenePopup) {
    m_openScenePopup = false;
    ImGui::OpenPopup("Open Scene");
  }
  if (m_saveScenePopup) {
    m_saveScenePopup = false;
    ImGui::OpenPopup("Save Scene As");
  }

  if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!m_world) {
      ImGui::TextUnformatted("No world loaded.");
    } else {
      ImGui::InputText("Path", m_scenePathBuf, sizeof(m_scenePathBuf));
      if (ImGui::Button("Open")) {
        const std::string path(m_scenePathBuf);
        if (!path.empty()) {
          const bool ok = (m_sceneManager != nullptr) && m_sceneManager->openScene(path);
          if (ok) {
            m_scenePath = path;
            m_sceneLoaded = true;
            markSceneClean(engine);
            m_sel.clear();
            m_hierarchy.setWorld(m_world);
            engine.rebuildEntityIndexMap();
            engine.rebuildRenderables();
            const auto &sky = m_world->skySettings();
            if (!sky.hdriPath.empty())
              engine.envIBL().loadFromHDR(sky.hdriPath);
            if (m_sceneManager && m_sceneManager->hasActive())
              m_scenePath = m_sceneManager->active().pathAbs;
          }
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!m_world) {
      ImGui::TextUnformatted("No world loaded.");
    } else {
      ImGui::InputText("Path", m_scenePathBuf, sizeof(m_scenePathBuf));
      if (ImGui::Button("Save")) {
        const std::string path(m_scenePathBuf);
        if (!path.empty()) {
          const bool ok = (m_sceneManager != nullptr) && m_sceneManager->saveActiveAs(path);
          if (ok) {
            m_scenePath = path;
            m_sceneLoaded = true;
            markSceneClean(engine);
            if (m_sceneManager && m_sceneManager->hasActive())
              m_scenePath = m_sceneManager->active().pathAbs;
          } else {
            Log::Warn("Failed to save scene to {}", path);
          }
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel"))
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

} // namespace Nyx
