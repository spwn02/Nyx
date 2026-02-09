#include "Application.h"

#include "AppContext.h"
#include "EngineContext.h"

#include "core/Log.h"

#include "editor/EditorLayer.h"
#include "editor/Selection.h"
#include "editor/tools/DockspaceLayout.h"
#include "editor/tools/EditorStateIO.h"
#include "editor/tools/ViewportPick.h"

#include "input/KeyCodes.h"
#include "platform/GLFWWindow.h"

#include "input/InputSystem.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/World.h"

#include <filesystem>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <vector>

#include <ImGuizmo.h>

namespace Nyx {

#include "Application_Helpers.inl"

void Application::initializeProjectAndSceneBindings() {
  std::filesystem::create_directories(cacheRootPath());
  m_projectManager.init(*m_engine, editorUserConfigPath());

  for (const auto &recent : m_projectManager.userCfg().recent.items) {
    if (m_projectManager.openProject(recent))
      break;
  }

  if (m_projectManager.hasProject()) {
    syncEditorStateFromProject(m_editorState, m_projectManager.runtime());
    m_editorState.lastProjectPath = m_projectManager.runtime().projectFileAbs();
    m_sceneManager.init(m_engine->world(), m_engine->materials(),
                        m_projectManager.runtime());
  } else {
    m_editorState.lastProjectPath.clear();
    m_editorState.lastScenePath.clear();
  }

  m_engine->world().setUUIDSeed(m_editorState.uuidSeed);

  if (m_app->editorLayer()) {
    m_app->editorLayer()->setWorld(&m_engine->world());
    m_app->editorLayer()->setProjectManager(&m_projectManager);
    m_app->editorLayer()->setSceneManager(&m_sceneManager);
    if (!m_projectManager.hasProject())
      m_app->editorLayer()->projectBrowserPanel().openModal();
    applyEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  }
}

void Application::tryLoadInitialScene(bool &loadedScene) {
  loadedScene = false;

  if (!m_editorState.lastScenePath.empty()) {
    const std::string resolvedPath =
        resolveScenePath(m_editorState.lastScenePath, m_editorState.lastProjectPath);

    if (std::filesystem::exists(resolvedPath)) {
      loadedScene = m_sceneManager.openScene(resolvedPath);

      if (loadedScene) {
        EditorStateIO::onSceneOpened(m_editorState, resolvedPath);
        if (m_app->editorLayer()) {
          m_app->editorLayer()->setScenePath(resolvedPath);
          m_app->editorLayer()->setWorld(&m_engine->world());
        }
        const auto &sky = m_engine->world().skySettings();
        if (!sky.hdriPath.empty()) {
          m_engine->envIBL().loadFromHDR(sky.hdriPath);
        }
      } else {
        Log::Warn("Failed to load scene from '{}'", resolvedPath);
      }
    } else {
      Log::Warn("Scene file does not exist: '{}'", resolvedPath);
    }
  } else {
    Log::Info("No last scene path configured");
  }

  if (loadedScene) {
    m_engine->rebuildEntityIndexMap();
    m_engine->rebuildRenderables();
    if (m_app->editorLayer())
      m_app->editorLayer()->setSceneLoaded(true);
  } else if (m_app->editorLayer()) {
    m_app->editorLayer()->defaultScene(*m_engine);
  }

  if (loadedScene && m_editorState.activeCamera) {
    EntityID cam = m_engine->world().findByUUID(m_editorState.activeCamera);
    if (cam != InvalidEntity && m_engine->world().hasCamera(cam)) {
      m_engine->world().setActiveCamera(cam);
      if (m_app->editorLayer())
        m_app->editorLayer()->setCameraEntity(cam);
    }
  }

  restoreAnimationClipState(m_editorState, *m_engine);
  if (m_app->editorLayer()) {
    auto *ed = m_app->editorLayer();
    ed->sequencerPanel().setWorld(ed->world());
    ed->sequencerPanel().setAnimationSystem(&m_engine->animation());
    ed->sequencerPanel().setAnimationClip(&m_engine->activeClip());
    ed->sequencerPanel().applyPersistState(m_editorState.sequencer);
  }
}

bool Application::handleWindowCloseRequests(bool &openUnsavedQuitPopup) {
  const auto hasUnsavedScene = [this]() -> bool {
    return m_sceneManager.hasActive() && m_sceneManager.active().dirty;
  };

  if (m_requestClose) {
    if (hasUnsavedScene())
      openUnsavedQuitPopup = true;
    else
      m_app->window().requestClose();
    m_requestClose = false;
  }

  if (m_app->window().shouldClose()) {
    if (hasUnsavedScene()) {
      m_app->window().cancelCloseRequest();
      openUnsavedQuitPopup = true;
    } else {
      return false;
    }
  }

  return true;
}

void Application::renderEditorOverlay(bool &openUnsavedQuitPopup,
                                      float &projectSaveTimer) {
  if (!m_app->isEditorVisible())
    return;

  m_app->imguiBegin();

  ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
  flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
           ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
  flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  const ImGuiViewport *vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("NyxDockspace", nullptr, flags);
  ImGui::PopStyleVar(2);

  ImGuiID dockspaceId = ImGui::GetID("NyxDockspaceID");
  m_engine->setDockspaceID(dockspaceId);
  ImGui::DockSpace(dockspaceId, ImVec2(0, 0),
                   ImGuiDockNodeFlags_PassthruCentralNode);

  if (auto *ed = m_app->editorLayer()) {
    if (imguiIniMissing())
      m_editorState.dockFallbackApplied = false;
    DockspaceLayout::applyDefaultLayoutIfNeeded(m_editorState, dockspaceId);
  }

  if (openUnsavedQuitPopup) {
    ImGui::OpenPopup("Unsaved Changes");
    openUnsavedQuitPopup = false;
  }
  if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Current scene has unsaved changes.");
    ImGui::TextUnformatted("Save before closing?");
    if (ImGui::Button("Save")) {
      bool ok = true;
      if (m_sceneManager.hasActive())
        ok = m_sceneManager.saveActive();
      if (ok) {
        if (auto *ed = m_app->editorLayer())
          ed->markSceneClean(*m_engine);
        ImGui::CloseCurrentPopup();
        m_app->window().requestClose();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save")) {
      if (auto *ed = m_app->editorLayer())
        ed->markSceneClean(*m_engine);
      else if (m_sceneManager.hasActive())
        m_sceneManager.active().dirty = false;
      ImGui::CloseCurrentPopup();
      m_app->window().requestClose();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  for (auto &layer : m_app->layers()) {
    layer->onImGui(*m_engine);
  }

  if (auto *ed = m_app->editorLayer()) {
    const std::string &scenePath = ed->scenePath();
    if (!scenePath.empty() && scenePath != m_editorState.lastScenePath) {
      m_editorState.lastScenePath = scenePath;
      m_editorState.pushRecentScene(scenePath);
      restoreAnimationClipState(m_editorState, *m_engine);
      captureEditorState(m_editorState, *ed, *m_engine);
      if (m_projectManager.hasProject()) {
        syncProjectFromEditorState(m_projectManager.runtime(), m_editorState);
        if (!m_projectManager.runtime().saveProject(
                m_projectManager.runtime().projectFileAbs()))
          Log::Warn("Failed to save project '{}'",
                    m_projectManager.runtime().projectFileAbs());
      }
    }
    m_editorState.autoSave = ed->autoSave();
    if (!ed->sceneLoaded()) {
      m_editorState.lastScenePath.clear();
    }

    if (projectSaveTimer >= 0.75f) {
      projectSaveTimer = 0.0f;
      captureEditorState(m_editorState, *ed, *m_engine);
      EditorStateIO::sanitizeBeforeSave(m_editorState);
      if (m_projectManager.hasProject()) {
        syncProjectFromEditorState(m_projectManager.runtime(), m_editorState);
        if (!m_projectManager.runtime().saveProject(
                m_projectManager.runtime().projectFileAbs()))
          Log::Warn("Failed to save project '{}'",
                    m_projectManager.runtime().projectFileAbs());
      }
    }
  }

  ImGui::End();
}

void Application::processInteractiveUpdate(float dt) {
  EditorLayer *ed = m_app->editorLayer();
  const bool editorVisible = m_app->isEditorVisible();
  auto &input = m_app->window().input();

  bool imguiWantsText = false;
  bool viewportHovered = false;
  bool gizmoWantsMouse = false;

  if (editorVisible) {
    ImGuiIO &io = ImGui::GetIO();
    imguiWantsText = io.WantTextInput;

    if (ed) {
      viewportHovered = ed->viewport().hovered;
      gizmoWantsMouse = ed->gizmoWantsMouse();
    }
  } else {
    viewportHovered = true;
  }

  const bool allowRmbCapture =
      viewportHovered && (!editorVisible || !imguiWantsText) &&
      !gizmoWantsMouse;
  (void)allowRmbCapture;

  if ((viewportHovered) && ed) {
    ed->cameraController().tick(*m_engine, *m_app, dt);
  }

  if (editorVisible && ed) {
    ImGuiIO &io = ImGui::GetIO();
    if (!io.WantTextInput) {
      if (!m_engine->uiBlockGlobalShortcuts()) {
        m_keybinds.process(input);
      }
      const bool seqHot = ed->sequencerPanel().timelineHot();
      if (seqHot)
        ed->sequencerPanel().handleStepRepeat(input, dt);
      if (input.isPressed(Key::Space)) {
        if (seqHot)
          ed->sequencerPanel().togglePlay();
        else
          m_engine->animation().toggle();
      }
    }
  } else {
    if (input.isPressed(Key::Space))
      m_engine->animation().toggle();
  }

  if (editorVisible && ed) {
    auto &vp = ed->viewport();
    const bool rmbCaptured = ed->cameraController().mouseCaptured;

    ImGuiIO &io = ImGui::GetIO();
    const bool canPick = vp.hovered && vp.hasImageRect() && !rmbCaptured &&
                         !io.WantTextInput &&
                         !ed->gizmoWantsMouse();

    if (canPick && input.isPressed(Key::MouseLeft)) {
      const double mx = input.state().mouseX;
      const double my = input.state().mouseY;

      ViewportImageRect r{};
      r.imageMin = {vp.imageMin.x, vp.imageMin.y};
      r.imageMax = {vp.imageMax.x, vp.imageMax.y};
      if (vp.lastRenderedSize.x > 0 && vp.lastRenderedSize.y > 0) {
        r.renderedSize = {vp.lastRenderedSize.x, vp.lastRenderedSize.y};
      } else {
        r.renderedSize = {vp.desiredSize.x, vp.desiredSize.y};
      }

      const auto pick = mapMouseToFramebufferPixel(mx, my, r);
      if (pick.inside) {
        const uint32_t px = pick.px;
        const uint32_t py = pick.py;
        m_engine->requestPick(px, py);
        m_pendingViewportPick = true;
        m_pendingPickCtrl = isCtrlDown(input);
        m_pendingPickShift = isShiftDown(input);
      }
    }
  }

  if (editorVisible && ed) {
    ed->syncWorldEvents(*m_engine);
  }

  if (!editorVisible && ed) {
    ed->sequencerPanel().setWorld(ed->world());
    ed->sequencerPanel().setAnimationSystem(&m_engine->animation());
    ed->sequencerPanel().setAnimationClip(&m_engine->activeClip());
    if (ed->world()) {
      std::vector<EntityID> exclude;
      exclude.push_back(ed->cameraEntity());
      exclude.push_back(ed->world()->activeCamera());
      ed->sequencerPanel().setHiddenExclusions(exclude);
      ed->sequencerPanel().setTrackExclusions(exclude);
    }
    ed->sequencerPanel().updateHiddenEntities();
    m_engine->setHiddenEntities(ed->sequencerPanel().hiddenEntities());
  }
  m_engine->tick(dt);
}

void Application::renderAndPresentFrame() {
  EditorLayer *ed = m_app->editorLayer();
  const bool editorVisible = m_app->isEditorVisible();
  auto &win = m_app->window();

  if (editorVisible && ed) {
    EntityID renderCam = InvalidEntity;
    if (ed->viewThroughCamera()) {
      const EntityID active = m_engine->world().activeCamera();
      if (active != InvalidEntity && m_engine->world().hasCamera(active))
        renderCam = active;
    }
    if (renderCam == InvalidEntity) {
      const EntityID editorCam = ed->cameraEntity();
      if (editorCam != InvalidEntity && m_engine->world().hasCamera(editorCam))
        renderCam = editorCam;
    }
    m_engine->setRenderCameraOverride(renderCam);
    if (ed->viewThroughCamera() && renderCam != InvalidEntity)
      m_engine->setHiddenEntity(renderCam);
    else
      m_engine->setHiddenEntity(InvalidEntity);
  } else {
    m_engine->setRenderCameraOverride(InvalidEntity);
    const EntityID active = m_engine->world().activeCamera();
    if (active != InvalidEntity && m_engine->world().hasCamera(active))
      m_engine->setHiddenEntity(active);
    else
      m_engine->setHiddenEntity(InvalidEntity);
  }

  uint32_t renderW = win.width();
  uint32_t renderH = win.height();

  if (editorVisible && ed) {
    const auto &vp = ed->viewport();
    renderW = std::max(1u, vp.desiredSize.x);
    renderH = std::max(1u, vp.desiredSize.y);
  }

  auto renderScene = [&](uint32_t w, uint32_t h) -> uint32_t {
    m_selectedPicksScratch.clear();
    if (editorVisible && ed) {
      const auto &sel = ed->selection();
      buildSelectedPicksForOutline(sel, m_selectedPicksScratch);
    }

    uint32_t activePick = 0u;
    if (editorVisible && ed) {
      const auto &sel = ed->selection();
      activePick = sel.activePick;
    }
    m_engine->setSelectionPickIDs(m_selectedPicksScratch, activePick);
    const uint32_t windowW = win.width();
    const uint32_t windowH = win.height();
    uint32_t viewportW = w;
    uint32_t viewportH = h;
    if (editorVisible && ed) {
      const auto &vp = ed->viewport();
      viewportW = std::max(1u, vp.desiredSize.x);
      viewportH = std::max(1u, vp.desiredSize.y);
    }
    return m_engine->render(windowW, windowH, viewportW, viewportH, w, h,
                            editorVisible);
  };

  uint32_t viewportTex = renderScene(renderW, renderH);

  if (editorVisible && ed) {
    auto &sel = ed->selection();

    if (m_pendingViewportPick) {
      m_pendingViewportPick = false;

      const uint32_t pid = m_engine->lastPickedID();
      applyViewportPickToSelection(*m_engine, pid, m_pendingPickCtrl,
                                   m_pendingPickShift, sel);

      viewportTex = renderScene(renderW, renderH);
    }
  }

  if (editorVisible && ed) {
    ed->setViewportTexture(viewportTex);
    ed->viewport().lastRenderedSize = {renderW, renderH};
  }

  if (editorVisible)
    m_app->imguiEnd();

  auto &input = win.input();
  input.endFrame();
  m_app->endFrame();
}

void Application::finalizeAndShutdown() {
  if (m_app->editorLayer())
    captureEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  EditorStateIO::sanitizeBeforeSave(m_editorState);
  if (m_projectManager.hasProject()) {
    const bool savedScenes = m_sceneManager.saveAllProjectScenes();
    if (savedScenes) {
      if (auto *ed = m_app->editorLayer())
        ed->markSceneClean(*m_engine);
      else if (m_sceneManager.hasActive())
        m_sceneManager.active().dirty = false;
    }
    syncProjectFromEditorState(m_projectManager.runtime(), m_editorState);
    if (!m_projectManager.runtime().saveProject(
            m_projectManager.runtime().projectFileAbs()))
      Log::Warn("Failed to save project '{}'",
                m_projectManager.runtime().projectFileAbs());
  }
  m_sceneManager.shutdown();
  m_projectManager.shutdown();
}

int Application::run() {
  float lastT = static_cast<float>(m_app->window().getTimeSeconds());
  float projectSaveTimer = 0.0f;
  initializeProjectAndSceneBindings();

  bool loadedScene = false;
  tryLoadInitialScene(loadedScene);
  bool openUnsavedQuitPopup = false;
  while (true) {
    if (!handleWindowCloseRequests(openUnsavedQuitPopup))
      break;

    const float nowT = static_cast<float>(m_app->window().getTimeSeconds());
    const float dt = std::max(0.0f, nowT - lastT);
    lastT = nowT;
    projectSaveTimer += dt;

    auto &win = m_app->window();
    auto &input = win.input();
    input.beginFrame();
    if (win.isMinimized() || !win.isVisible()) {
      win.waitEventsTimeout(0.1);
      input.endFrame();
      lastT = static_cast<float>(m_app->window().getTimeSeconds());
      continue;
    }
    m_app->beginFrame();

    if (input.isPressed(Key::F)) {
      const bool wasVisible = m_app->isEditorVisible();
      m_app->toggleEditorOverlay();

      if (!wasVisible && m_app->isEditorVisible()) {
        if (m_app->editorLayer())
          m_app->editorLayer()->setWorld(&m_engine->world());
      }
    }

    renderEditorOverlay(openUnsavedQuitPopup, projectSaveTimer);
    processInteractiveUpdate(dt);
    renderAndPresentFrame();
  }

  finalizeAndShutdown();

  return 0;
}

} // namespace Nyx
