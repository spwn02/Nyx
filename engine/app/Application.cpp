#include "Application.h"

#include "AppContext.h"
#include "EngineContext.h"

#include "../core/Assert.h"
#include "../core/Log.h"

#include "../editor/EditorLayer.h"
#include "../editor/Selection.h"
#include "editor/tools/DockspaceLayout.h"
#include "editor/tools/EditorStateIO.h"
#include "editor/tools/ProjectSerializer.h"
#include "editor/tools/ViewportPick.h"

#include "../input/KeyCodes.h"
#include "../platform/GLFWWindow.h"

#include "input/InputSystem.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/World.h"
#include "scene/WorldSerializer.h"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <vector>

#include <ImGuizmo.h>

namespace Nyx {

static float getTimeSeconds() { return static_cast<float>(glfwGetTime()); }

Application::Application(std::unique_ptr<AppContext> app,
                         std::unique_ptr<EngineContext> engine)
    : m_app(std::move(app)), m_engine(std::move(engine)) {
  NYX_ASSERT(m_app != nullptr, "Application requires AppContext");
  NYX_ASSERT(m_engine != nullptr, "Application requires EngineContext");
}

Application::~Application() = default;

static bool isShiftDown(const InputSystem &in) {
  return in.isDown(Key::LeftShift) || in.isDown(Key::RightShift);
}
static bool isCtrlDown(const InputSystem &in) {
  return in.isDown(Key::LeftCtrl) || in.isDown(Key::RightCtrl);
}

static EntityID resolvePickEntity(EngineContext &engine, const Selection &sel,
                                  uint32_t pid) {
  EntityID e = sel.entityForPick(pid);
  if (e != InvalidEntity)
    return e;
  const uint32_t slotIndex = pickEntity(pid);
  return engine.resolveEntityIndex(slotIndex);
}

static void applyViewportPickToSelection(EngineContext &engine, uint32_t pid,
                                         bool ctrl, bool shift,
                                         Selection &sel) {
  const EntityID e = resolvePickEntity(engine, sel, pid);
  if (pid == 0u || e == InvalidEntity) {
    if (!ctrl)
      sel.clear();
    return;
  }

  if (ctrl) {
    sel.togglePick(pid, e);
  } else if (shift) {
    sel.addPick(pid, e);
  } else {
    sel.setSinglePick(pid, e);
  }

  sel.activeEntity = e;
}

static void buildSelectedPicksForOutline(const Selection &sel,
                                         std::vector<uint32_t> &out) {
  out.clear();
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;
  out = sel.picks;
}

static void deleteSelection(World &world, Selection &sel) {
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;

  // Delete by unique entities (submesh-aware selection still deletes entity)
  std::vector<EntityID> ents;
  ents.reserve(sel.picks.size());
  for (uint32_t p : sel.picks) {
    EntityID e = sel.entityForPick(p);
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index < b.index;
    return a.generation < b.generation;
  });
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Optional: delete children safely by descending ID
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index > b.index;
    return a.generation > b.generation;
  });

  for (EntityID e : ents) {
    if (world.isAlive(e))
      world.destroyEntity(e);
  }
  sel.clear();
}

static bool ancestorIsInSet(World &world, EntityID e,
                            const std::vector<EntityID> &set) {
  EntityID p = world.parentOf(e);
  while (p != InvalidEntity) {
    if (std::find(set.begin(), set.end(), p) != set.end())
      return true;
    p = world.parentOf(p);
  }
  return false;
}

static void duplicateMaterialsForSubtree(World &world,
                                         MaterialSystem &materials,
                                         EntityID root) {
  if (!world.isAlive(root))
    return;

  if (world.hasMesh(root)) {
    auto &mc = world.ensureMesh(root);
    for (auto &sm : mc.submeshes) {
      if (sm.material != InvalidMaterial && materials.isAlive(sm.material)) {
        MaterialData copy = materials.cpu(sm.material);
        sm.material = materials.create(copy);
      }
    }
  }

  EntityID c = world.hierarchy(root).firstChild;
  while (c != InvalidEntity) {
    EntityID next = world.hierarchy(c).nextSibling;
    duplicateMaterialsForSubtree(world, materials, c);
    c = next;
  }
}

static void duplicateSelection(EngineContext &engine, Selection &sel) {
  World &world = engine.world();
  if (sel.kind != SelectionKind::Picks || sel.picks.empty())
    return;

  // Duplicate top-level entities only; preserve hierarchy by cloning subtree
  std::vector<EntityID> ents;
  ents.reserve(sel.picks.size());
  for (uint32_t p : sel.picks) {
    EntityID e = sel.entityForPick(p);
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end(), [](EntityID a, EntityID b) {
    if (a.index != b.index)
      return a.index < b.index;
    return a.generation < b.generation;
  });
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Filter out entities whose ancestor is also selected
  std::vector<EntityID> top;
  top.reserve(ents.size());
  for (EntityID e : ents) {
    if (!ancestorIsInSet(world, e, ents))
      top.push_back(e);
  }

  std::vector<uint32_t> newPicks;
  std::vector<EntityID> newEntities;
  for (EntityID e : top) {
    EntityID dup = world.cloneSubtree(e, InvalidEntity);
    if (dup == InvalidEntity)
      continue;

    duplicateMaterialsForSubtree(world, engine.materials(), dup);

    // pick submesh0 of cloned entity if it exists, else packPick(dup,0)
    const uint32_t pid = packPick(dup, 0);
    newPicks.push_back(pid);
    newEntities.push_back(dup);
  }

  if (!newPicks.empty()) {
    sel.kind = SelectionKind::Picks;
    sel.picks = newPicks;
    sel.pickEntity.clear();
    for (size_t i = 0; i < newPicks.size(); ++i)
      sel.pickEntity.emplace(newPicks[i], newEntities[i]);
    sel.activePick = newPicks.back();
    sel.activeEntity = sel.entityForPick(sel.activePick);
  }
}

static bool imguiIniMissing() {
  ImGuiIO &io = ImGui::GetIO();
  if (!io.IniFilename || io.IniFilename[0] == 0)
    return true;
  return !std::filesystem::exists(std::filesystem::absolute(io.IniFilename));
}

static std::string projectStatePath() {
  return (std::filesystem::current_path() / ".nyx" / "project.nyxproj.json")
      .string();
}

static std::string resolveScenePath(const std::string &scenePath,
                                    const std::string &projectPath) {
  if (scenePath.empty())
    return scenePath;
  std::filesystem::path p(scenePath);
  if (p.is_relative()) {
    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path cand = (cwd / p).lexically_normal();
    if (std::filesystem::exists(cand))
      return cand.string();

    std::filesystem::path base =
        std::filesystem::path(projectPath).parent_path();
    if (!base.empty()) {
      std::filesystem::path candProj = (base / p).lexically_normal();
      if (std::filesystem::exists(candProj))
        return candProj.string();
    }

    return cand.string();
  }
  return p.lexically_normal().string();
}

static void applyEditorState(EditorState &st, EditorLayer &ed,
                             EngineContext &engine) {
  auto &ps = ed.persist();
  ps.panels.viewport = st.panels.showViewport;
  ps.panels.hierarchy = st.panels.showHierarchy;
  ps.panels.inspector = st.panels.showInspector;
  ps.panels.assetBrowser = st.panels.showAssets;
  ps.panels.stats = st.panels.showStats;

  ed.gizmo().op = st.gizmoOp;
  ed.gizmo().mode = st.gizmoMode;
  ed.setAutoSave(st.autoSave);
  ed.setScenePath(st.lastScenePath);

  engine.setViewMode(st.viewport.viewMode);
}

static void captureEditorState(EditorState &st, EditorLayer &ed,
                               EngineContext &engine) {
  const auto &ps = ed.persist();
  st.panels.showViewport = ps.panels.viewport;
  st.panels.showHierarchy = ps.panels.hierarchy;
  st.panels.showInspector = ps.panels.inspector;
  st.panels.showAssets = ps.panels.assetBrowser;
  st.panels.showStats = ps.panels.stats;

  st.gizmoOp = ed.gizmo().op;
  st.gizmoMode = ed.gizmo().mode;
  st.autoSave = ed.autoSave();
  st.lastScenePath = ed.scenePath();

  st.viewport.viewMode = engine.viewMode();

  const EntityID active = engine.world().activeCamera();
  st.activeCamera = engine.world().uuid(active);
  st.uuidSeed = engine.world().uuidSeed();
}

int Application::run() {
  float lastT = getTimeSeconds();

  const std::string projPath = projectStatePath();
  m_editorState.lastProjectPath = projPath;
  ProjectSerializer::loadFromFile(m_editorState, projPath);

  m_engine->world().setUUIDSeed(m_editorState.uuidSeed);

  if (m_app->editorLayer()) {
    m_app->editorLayer()->setWorld(&m_engine->world());
    applyEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  }

  bool loadedScene = false;
  Log::Info("Last scene path: '{}'", m_editorState.lastScenePath);
  
  if (!m_editorState.lastScenePath.empty()) {
    const std::string resolvedPath = resolveScenePath(m_editorState.lastScenePath, projPath);
    Log::Info("Resolved scene path: '{}'", resolvedPath);
    
    if (std::filesystem::exists(resolvedPath)) {
      Log::Info("Scene file exists, attempting to load...");
      m_engine->resetMaterials();
      loadedScene = WorldSerializer::loadFromFile(m_engine->world(),
                                                  m_engine->materials(),
                                                  resolvedPath);
      
      if (loadedScene) {
        Log::Info("Scene loaded successfully");
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

  // Editor camera entity (ECS-driven)
  if (loadedScene && m_editorState.activeCamera) {
    EntityID cam = m_engine->world().findByUUID(m_editorState.activeCamera);
    if (cam != InvalidEntity && m_engine->world().hasCamera(cam)) {
      m_engine->world().setActiveCamera(cam);
      if (m_app->editorLayer())
        m_app->editorLayer()->setCameraEntity(cam);
    }
  }

  while (!m_app->window().shouldClose()) {
    const float nowT = getTimeSeconds();
    const float dt = std::max(0.0f, nowT - lastT);
    lastT = nowT;

    // ------------------------------------------------------------
    // Begin frame
    // ------------------------------------------------------------
    auto &win = m_app->window();
    auto &input = win.input();
    input.beginFrame();
    if (win.isMinimized() || !win.isVisible()) {
      // Keep pumping events while minimized/hidden to avoid OS "not responding".
      win.waitEventsTimeout(0.1);
      input.endFrame();
      lastT = getTimeSeconds();
      continue;
    }
    m_app->beginFrame();

    // ------------------------------------------------------------
    // Global toggles
    // ------------------------------------------------------------
    if (input.isPressed(Key::F)) {
      // Contract: F toggles editor overlay by removing/adding editor layer.
      // IMPORTANT: preserve editor state inside EditorLayer (dock layout, etc).
      const bool wasVisible = m_app->isEditorVisible();
      m_app->toggleEditorOverlay();

      // When hiding editor, release capture so it doesn't "go insane"
      if (wasVisible && !m_app->isEditorVisible()) {
        m_app->editorLayer()->cameraController().captureMouse(false, win);
      }

      // When showing editor again, also ensure capture is released
      // (user should RMB-capture explicitly inside viewport).
      if (!wasVisible && m_app->isEditorVisible()) {
        m_app->editorLayer()->cameraController().captureMouse(false, win);

        if (m_app->editorLayer())
          m_app->editorLayer()->setWorld(&m_engine->world());
      }
    }

    if (input.isPressed(Key::Escape)) {
      // ESC always releases mouse capture
      m_app->editorLayer()->cameraController().captureMouse(false, win);
    }

    // ------------------------------------------------------------
    // Build UI (editor visible)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible()) {
      m_app->imguiBegin();

      ImGuiWindowFlags flags =
          ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
      flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
      flags |=
          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

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

      for (auto &layer : m_app->layers()) {
        layer->onImGui(*m_engine);
      }

      if (auto *ed = m_app->editorLayer()) {
        const std::string &scenePath = ed->scenePath();
        if (!scenePath.empty() && scenePath != m_editorState.lastScenePath) {
          m_editorState.lastScenePath = scenePath;
          m_editorState.pushRecentScene(scenePath);
          ProjectSerializer::saveToFile(m_editorState,
                                        m_editorState.lastProjectPath);
        }
        m_editorState.autoSave = ed->autoSave();
        if (!ed->sceneLoaded()) {
          m_editorState.lastScenePath.clear();
        }
      }

      ImGui::End(); // Dockspace host
    }

    // ------------------------------------------------------------
    // Capture rules + viewport gating
    // ------------------------------------------------------------
    bool imguiWantsText = false;
    bool viewportHovered = false;
    bool viewportFocused = false;
    bool gizmoWantsMouse = false;

    if (m_app->isEditorVisible()) {
      ImGuiIO &io = ImGui::GetIO();
      imguiWantsText = io.WantTextInput;

      if (auto *ed = m_app->editorLayer()) {
        viewportHovered = ed->viewport().hovered; // SHOULD be image-hovered
        viewportFocused = ed->viewport().focused;
        gizmoWantsMouse = ed->gizmoWantsMouse();
      }
    } else {
      viewportHovered = true;
      viewportFocused = true;
    }

    // RMB capture allowed only in viewport and when not typing
    const bool allowRmbCapture =
        viewportHovered && (!m_app->isEditorVisible() || !imguiWantsText) &&
        !gizmoWantsMouse;

    if (allowRmbCapture && input.isPressed(Key::MouseRight)) {
      m_app->editorLayer()->cameraController().captureMouse(true, win);
    }
    if (input.isReleased(Key::MouseRight)) {
      m_app->editorLayer()->cameraController().captureMouse(false, win);
    }

    // ------------------------------------------------------------
    // Camera movement
    // ------------------------------------------------------------
    m_app->editorLayer()->cameraController().tick(*m_engine, *m_app, dt);

    // ------------------------------------------------------------
    // Keybinds (editor only; ignore when typing)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      ImGuiIO &io = ImGui::GetIO();
      if (!io.WantTextInput) {
        auto &sel = m_app->editorLayer()->selection();
        auto &gizmo = m_app->editorLayer()->gizmo();
        const bool rmbCaptured =
            m_app->editorLayer()->cameraController().mouseCaptured;

        // Clear selection
        if (input.isPressed(Key::Space)) {
          sel.clear();
          m_engine->setSelectionPickIDs({});
        }

        // Duplicate: Shift + D
        if (isShiftDown(input) && input.isPressed(Key::D)) {
          duplicateSelection(*m_engine, sel);
        }

        // Delete: X or Delete
        if (input.isPressed(Key::X) || input.isPressed(Key::Delete)) {
          deleteSelection(m_engine->world(), sel);
        }

        if (!rmbCaptured) {
          if (input.isPressed(Key::W))
            gizmo.op = GizmoOp::Translate;
          if (input.isPressed(Key::E))
            gizmo.op = GizmoOp::Rotate;
          if (input.isPressed(Key::R))
            gizmo.op = GizmoOp::Scale;
          if (input.isPressed(Key::Q))
            gizmo.mode = (gizmo.mode == GizmoMode::Local) ? GizmoMode::World
                                                          : GizmoMode::Local;
        }
      }
    }

    // ------------------------------------------------------------
    // Click-to-pick (editor viewport only)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto &vp = m_app->editorLayer()->viewport();
      const bool rmbCaptured =
          m_app->editorLayer()->cameraController().mouseCaptured;

      ImGuiIO &io = ImGui::GetIO();
      const bool canPick = vp.hovered && vp.hasImageRect() && !rmbCaptured &&
                           !io.WantTextInput &&
                           !m_app->editorLayer()->gizmoWantsMouse();

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

    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      m_app->editorLayer()->syncWorldEvents();
    }

    // ------------------------------------------------------------
    // Engine tick
    // ------------------------------------------------------------
    m_engine->tick(dt);
    ;
    // ------------------------------------------------------------
    // Render camera override (editor viewport)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto *ed = m_app->editorLayer();
      EntityID renderCam = InvalidEntity;
      if (ed->viewThroughCamera()) {
        const EntityID active = m_engine->world().activeCamera();
        if (active != InvalidEntity && m_engine->world().hasCamera(active))
          renderCam = active;
      }
      if (renderCam == InvalidEntity) {
        const EntityID editorCam = ed->cameraEntity();
        if (editorCam != InvalidEntity &&
            m_engine->world().hasCamera(editorCam))
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
    // ------------------------------------------------------------
    // Determine render target size
    // ------------------------------------------------------------
    uint32_t renderW = win.width();
    uint32_t renderH = win.height();

    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      const auto &vp = m_app->editorLayer()->viewport();
      renderW = std::max(1u, vp.desiredSize.x);
      renderH = std::max(1u, vp.desiredSize.y);
    }

    // ------------------------------------------------------------
    // Render lambda (uses selection picks for outline)
    // ------------------------------------------------------------
    auto renderScene = [&](uint32_t w, uint32_t h) -> uint32_t {
      std::vector<uint32_t> selPick;

      if (m_app->isEditorVisible() && m_app->editorLayer()) {
        const auto &sel = m_app->editorLayer()->selection();
        buildSelectedPicksForOutline(sel, selPick);
      }

      m_engine->setSelectionPickIDs(selPick);
      const uint32_t windowW = win.width();
      const uint32_t windowH = win.height();
      uint32_t viewportW = w;
      uint32_t viewportH = h;
      if (m_app->isEditorVisible() && m_app->editorLayer()) {
        const auto &vp = m_app->editorLayer()->viewport();
        viewportW = std::max(1u, vp.desiredSize.x);
        viewportH = std::max(1u, vp.desiredSize.y);
      }
      return m_engine->render(windowW, windowH, viewportW, viewportH, w, h,
                              m_app->isEditorVisible());
    };

    uint32_t viewportTex = renderScene(renderW, renderH);

    // ------------------------------------------------------------
    // Resolve pending pick AFTER render (pick pass writes ID texture)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto &sel = m_app->editorLayer()->selection();

      if (m_pendingViewportPick) {
        m_pendingViewportPick = false;

        const uint32_t pid = m_engine->lastPickedID(); // packed entity+submesh
        applyViewportPickToSelection(*m_engine, pid, m_pendingPickCtrl,
                                     m_pendingPickShift, sel);

        // Re-render immediately so outline matches selection this frame
        viewportTex = renderScene(renderW, renderH);
      }
    }

    // ------------------------------------------------------------
    // Submit texture to viewport
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      m_app->editorLayer()->setViewportTexture(viewportTex);
      m_app->editorLayer()->viewport().lastRenderedSize = {renderW, renderH};
    }

    // ------------------------------------------------------------
    // Finalize ImGui
    // ------------------------------------------------------------
    if (m_app->isEditorVisible()) {
      m_app->imguiEnd();
    }

    // ------------------------------------------------------------
    // End frame
    // ------------------------------------------------------------
    input.endFrame();
    m_app->endFrame();
  }

  if (m_app->editorLayer())
    captureEditorState(m_editorState, *m_app->editorLayer(), *m_engine);
  EditorStateIO::sanitizeBeforeSave(m_editorState);
  ProjectSerializer::saveToFile(m_editorState, m_editorState.lastProjectPath);

  return 0;
}

} // namespace Nyx
