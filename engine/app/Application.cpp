#include "Application.h"

#include "AppContext.h"
#include "EngineContext.h"

#include "../core/Assert.h"
#include "../core/Log.h"

#include "../editor/EditorLayer.h"
#include "../editor/Selection.h"

#include "../input/KeyCodes.h"
#include "../platform/GLFWWindow.h"

#include "editor/EditorDockLayout.h"
#include "input/InputSystem.h"
#include "scene/EntityID.h"
#include "scene/Pick.h"
#include "scene/World.h"

#include <GLFW/glfw3.h>
#include <filesystem>
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

static void releaseMouseCapture(GLFWwindow *w, EditorCamera &cam) {
  if (!cam.mouseCaptured)
    return;
  glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  cam.mouseCaptured = false;
}

static void captureMouse(GLFWwindow *w, EditorCamera &cam) {
  if (cam.mouseCaptured)
    return;
  glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  cam.mouseCaptured = true;
}

static void applyViewportPickToSelection(const World &world, uint32_t pid,
                                         bool ctrl, bool shift,
                                         Selection &sel) {
  const uint32_t slotIndex = pickEntity(pid);
  const EntityID e = world.entityFromSlotIndex(slotIndex);
  if (pid == 0u || e == InvalidEntity) {
    if (!ctrl)
      sel.clear();
    return;
  }

  if (ctrl) {
    sel.togglePick(pid);
  } else if (shift) {
    sel.addPick(pid);
  } else {
    sel.setSinglePick(pid);
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
    EntityID e = world.entityFromSlotIndex(pickEntity(p));
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end());
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Optional: delete children safely by descending ID
  std::sort(ents.begin(), ents.end(), std::greater<EntityID>());

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
    auto &mc = world.mesh(root);
    for (auto &sm : mc.submeshes) {
      if (sm.material != InvalidMaterial && materials.isAlive(sm.material)) {
        MaterialData copy = materials.get(sm.material);
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
    EntityID e = world.entityFromSlotIndex(pickEntity(p));
    if (e != InvalidEntity)
      ents.push_back(e);
  }
  std::sort(ents.begin(), ents.end());
  ents.erase(std::unique(ents.begin(), ents.end()), ents.end());

  // Filter out entities whose ancestor is also selected
  std::vector<EntityID> top;
  top.reserve(ents.size());
  for (EntityID e : ents) {
    if (!ancestorIsInSet(world, e, ents))
      top.push_back(e);
  }

  std::vector<uint32_t> newPicks;
  for (EntityID e : top) {
    EntityID dup = world.cloneSubtree(e, InvalidEntity);
    if (dup == InvalidEntity)
      continue;

    duplicateMaterialsForSubtree(world, engine.materials(), dup);

    // pick submesh0 of cloned entity if it exists, else packPick(dup,0)
    const uint32_t pid = packPick(dup, 0);
    newPicks.push_back(pid);
  }

  if (!newPicks.empty()) {
    sel.kind = SelectionKind::Picks;
    sel.picks = newPicks;
    sel.activePick = newPicks.back();
    sel.activeEntity = world.entityFromSlotIndex(pickEntity(sel.activePick));
  }
}

static bool imguiIniMissing() {
  ImGuiIO &io = ImGui::GetIO();
  if (!io.IniFilename || io.IniFilename[0] == 0)
    return true;
  return !std::filesystem::exists(std::filesystem::absolute(io.IniFilename));
}

static bool dockNeedsBootstrap(ImGuiID dockspaceId) {
  ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspaceId);
  if (!node)
    return true;

  // If there are no splits and no docked windows, it’s basically empty.
  const bool hasChildren =
      (node->ChildNodes[0] != nullptr) || (node->ChildNodes[1] != nullptr);
  const bool hasDockedWindows = (node->Windows.Size > 0);
  return (!hasChildren && !hasDockedWindows);
}

int Application::run() {
  float lastT = getTimeSeconds();

  if (m_app->editorLayer()) {
    m_app->editorLayer()->setWorld(&m_engine->world());
  }

  // Demo spawn (optional)
  {
    EntityID e = m_engine->world().createEntity("Cube");
    auto &mc = m_engine->world().ensureMesh(e, ProcMeshType::Cube, 1);
    if (mc.submeshes.empty())
      mc.submeshes.push_back(MeshSubmesh{.name = "Submesh 0",
                                         .type = ProcMeshType::Cube,
                                         .material = InvalidMaterial});
    mc.submeshes[0].type = ProcMeshType::Cube;
    (void)m_engine->world().materialHandle(e, 0);

    auto &tr = m_engine->world().transform(e);
    tr.translation = {0.0f, 0.0f, 0.0f};
    tr.scale = {1.0f, 1.0f, 1.0f};

    if (m_app->editorLayer()) {
      auto &sel = m_app->editorLayer()->selection();
      sel.setSinglePick(packPick(e, 0));
      sel.activeEntity = e;
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
        releaseMouseCapture(win.handle(), m_app->editorLayer()->camera());
      }

      // When showing editor again, also ensure capture is released
      // (user should RMB-capture explicitly inside viewport).
      if (!wasVisible && m_app->isEditorVisible()) {
        releaseMouseCapture(win.handle(), m_app->editorLayer()->camera());

        if (m_app->editorLayer())
          m_app->editorLayer()->setWorld(&m_engine->world());
      }
    }

    if (input.isPressed(Key::Escape)) {
      // ESC always releases mouse capture
      releaseMouseCapture(win.handle(), m_app->editorLayer()->camera());
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
        auto &ps = ed->persist();

        const bool needs = imguiIniMissing() || dockNeedsBootstrap(dockspaceId);

        if (needs && !ps.dockLayoutApplied) {
          const ImGuiViewport *vp = ImGui::GetMainViewport();
          BuildDefaultDockLayout(dockspaceId, vp->WorkSize);
          ps.dockLayoutApplied = true;
        }
      }

      for (auto &layer : m_app->layers()) {
        layer->onImGui(*m_engine);
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
      captureMouse(win.handle(), m_app->editorLayer()->camera());
    }
    if (input.isReleased(Key::MouseRight)) {
      releaseMouseCapture(win.handle(), m_app->editorLayer()->camera());
    }

    // ------------------------------------------------------------
    // Camera movement
    // ------------------------------------------------------------
    if (m_app->editorLayer()->camera().mouseCaptured) {
      auto yaw = m_app->editorLayer()->camera().yawDeg;
      yaw += float(input.state().mouseDeltaX) *
             m_app->editorLayer()->camera().sensitivity;
      auto pitch = m_app->editorLayer()->camera().pitchDeg;
      pitch -= std::clamp(float(input.state().mouseDeltaY) *
                              m_app->editorLayer()->camera().sensitivity,
                          -120.0f, 120.0f);
      m_app->editorLayer()->camera().setYawPitch(yaw, pitch);

      glm::vec3 front = m_app->editorLayer()->camera().front();
      glm::vec3 right = m_app->editorLayer()->camera().right();

      float speed = m_app->editorLayer()->camera().speed * dt;
      if (isShiftDown(input))
        speed *= 2.0f;

      glm::vec3 moveDelta(0.0f);
      if (input.isDown(Key::W))
        moveDelta += front * speed;
      if (input.isDown(Key::S))
        moveDelta -= front * speed;
      if (input.isDown(Key::A))
        moveDelta -= right * speed;
      if (input.isDown(Key::D))
        moveDelta += right * speed;
      if (input.isDown(Key::Q))
        moveDelta.y -= speed;
      if (input.isDown(Key::E))
        moveDelta.y += speed;

      if (moveDelta != glm::vec3(0.0f)) {
        m_app->editorLayer()->camera().position += moveDelta;
        m_app->editorLayer()->camera().markViewDirty();
      }
    }

    // ------------------------------------------------------------
    // Keybinds (editor only; ignore when typing)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      ImGuiIO &io = ImGui::GetIO();
      if (!io.WantTextInput) {
        auto &sel = m_app->editorLayer()->selection();

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
      }
    }

    // ------------------------------------------------------------
    // Click-to-pick (editor viewport only)
    // ------------------------------------------------------------
    if (m_app->isEditorVisible() && m_app->editorLayer()) {
      auto &vp = m_app->editorLayer()->viewport();
      const bool rmbCaptured = m_app->editorLayer()->camera().mouseCaptured;

      ImGuiIO &io = ImGui::GetIO();
      const bool canPick = vp.hovered && vp.hasImageRect() && !rmbCaptured &&
                           !io.WantTextInput &&
                           !m_app->editorLayer()->gizmoWantsMouse();

      if (canPick && input.isPressed(Key::MouseLeft)) {
        const double mx = input.state().mouseX;
        const double my = input.state().mouseY;

        const float u =
            float((mx - vp.imageMin.x) / (vp.imageMax.x - vp.imageMin.x));
        const float v =
            float((my - vp.imageMin.y) / (vp.imageMax.y - vp.imageMin.y));

        if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
          const uint32_t fbW = std::max(1u, vp.desiredSize.x);
          const uint32_t fbH = std::max(1u, vp.desiredSize.y);

          const uint32_t px = static_cast<uint32_t>(u * float(fbW));
          const uint32_t py = static_cast<uint32_t>(v * float(fbH));

          m_engine->requestPick(px, py);
          m_pendingViewportPick = true;
          m_pendingPickCtrl = isCtrlDown(input);
          m_pendingPickShift = isShiftDown(input);
        }
      }
    }

    // ------------------------------------------------------------
    // Engine tick
    // ------------------------------------------------------------
    m_engine->tick(dt);
    ;
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
      return m_engine->render(w, h, m_app->editorLayer()->camera(),
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
        applyViewportPickToSelection(m_engine->world(), pid, m_pendingPickCtrl,
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

  return 0;
}

} // namespace Nyx
