#include "EditorLayer.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "editor/EditorDockLayout.h"
#include "editor/EditorPersist.h"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "scene/Pick.h"
#include <filesystem>
#include <glm/glm.hpp>
#include <imgui.h>

#include <ImGuizmo.h>
#include <unordered_set>

namespace Nyx {

static std::string editorStatePath() {
  return std::filesystem::current_path() / ".nyx" / "editor_state.ini";
}

void EditorLayer::onAttach() {
  auto res = EditorPersist::load(editorStatePath(), m_persist);
  if (!res) {
    Log::Warn("EditorPersist load failed: {}", res.error());
  }
}

void EditorLayer::onDetach() {
  auto res = EditorPersist::save(editorStatePath(), m_persist);
  if (!res) {
    Log::Warn("EditorPersist save failed: {}", res.error());
  }
}

static const char *meshTypeName(ProcMeshType t) {
  switch (t) {
  case ProcMeshType::Cube:
    return "Cube";
  case ProcMeshType::Plane:
    return "Plane";
  case ProcMeshType::Circle:
    return "Circle";
  case ProcMeshType::Sphere:
    return "Sphere";
  case ProcMeshType::Monkey:
    return "Monkey";
  default:
    return "Unknown";
  }
}

static bool meshTypeCombo(const char *label, ProcMeshType &t) {
  const char *cur = meshTypeName(t);
  bool changed = false;
  if (ImGui::BeginCombo(label, cur)) {
    auto item = [&](ProcMeshType v) {
      bool isSel = (t == v);
      if (ImGui::Selectable(meshTypeName(v), isSel)) {
        t = v;
        changed = true;
      }
      if (isSel)
        ImGui::SetItemDefaultFocus();
    };
    item(ProcMeshType::Cube);
    item(ProcMeshType::Plane);
    item(ProcMeshType::Circle);
    item(ProcMeshType::Sphere);
    item(ProcMeshType::Monkey);
    ImGui::EndCombo();
  }
  return changed;
}

// static glm::vec3 cameraFront(float yawDeg, float pitchDeg) {
//   const float yaw = glm::radians(yawDeg);
//   const float pitch = glm::radians(pitchDeg);
//   glm::vec3 f{cos(yaw) * cos(pitch), sin(pitch), sin(yaw) * cos(pitch)};
//   return glm::normalize(f);
// }

static void drawGizmoToolbar(GizmoState &g) {
  ImGui::Begin("Gizmo");

  if (ImGui::RadioButton("T", g.op == GizmoOp::Translate))
    g.op = GizmoOp::Translate;
  ImGui::SameLine();
  if (ImGui::RadioButton("R", g.op == GizmoOp::Rotate))
    g.op = GizmoOp::Rotate;
  ImGui::SameLine();
  if (ImGui::RadioButton("S", g.op == GizmoOp::Scale))
    g.op = GizmoOp::Scale;

  ImGui::SameLine();
  ImGui::SeparatorText("");

  if (ImGui::RadioButton("Local", g.mode == GizmoMode::Local))
    g.mode = GizmoMode::Local;
  ImGui::SameLine();
  if (ImGui::RadioButton("World", g.mode == GizmoMode::World))
    g.mode = GizmoMode::World;

  ImGui::Checkbox("Snap", &g.useSnap);
  if (g.useSnap) {
    ImGui::DragFloat("Snap T", &g.snapTranslate, 0.1f, 0.001f, 100.0f);
    ImGui::DragFloat("Snap R", &g.snapRotateDeg, 1.0f, 0.1f, 180.0f);
    ImGui::DragFloat("Snap S", &g.snapScale, 0.1f, 0.01f, 10.0f);
  }

  ImGui::End();
}

static ImGuizmo::OPERATION toImGuizmoOp(GizmoOp op) {
  switch (op) {
  case GizmoOp::Translate:
    return ImGuizmo::TRANSLATE;
  case GizmoOp::Rotate:
    return ImGuizmo::ROTATE;
  case GizmoOp::Scale:
    return ImGuizmo::SCALE;
  }
  return ImGuizmo::TRANSLATE;
}

static ImGuizmo::MODE toImGuizmoMode(GizmoMode m) {
  return m == GizmoMode::World ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
}

void EditorLayer::drawViewport(EngineContext &engine) {
  ImGui::Begin("Viewport");

  if (m_viewportTex != 0) {
    ImGuiIO &io = ImGui::GetIO();

    // Available size in *ImGui logical units*
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 1)
      avail.x = 1;
    if (avail.y < 1)
      avail.y = 1;

    // Convert to framebuffer pixels (DPI perfect)
    uint32_t pxW =
        (uint32_t)std::max(1.0f, avail.x * io.DisplayFramebufferScale.x);
    uint32_t pxH =
        (uint32_t)std::max(1.0f, avail.y * io.DisplayFramebufferScale.y);

    m_viewport.desiredSize = {pxW, pxH};

    // Draw the viewport image (use avail size in logical units)
    ImTextureID tid = (ImTextureID)(uintptr_t)m_viewportTex;
    ImGui::Image(tid, avail, ImVec2(0, 1), ImVec2(1, 0)); // flip Y if needed

    m_viewport.imageMin = {ImGui::GetItemRectMin().x,
                           ImGui::GetItemRectMin().y};
    m_viewport.imageMax = {ImGui::GetItemRectMax().x,
                           ImGui::GetItemRectMax().y};

    m_viewport.hovered = ImGui::IsItemHovered();
    m_viewport.focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  }

  // --- ImGuizmo ---
  ImGuizmo::BeginFrame();

  Selection &sel = m_sel;
  const uint32_t activePick =
      sel.activePick ? sel.activePick
                     : (sel.picks.empty() ? 0u : sel.picks.front());
  m_gizmoUsing = false;
  m_gizmoOver = false;

  if (sel.kind == SelectionKind::Picks && activePick != 0u) {
    EntityID slot = pickEntity(activePick);
    EntityID e = engine.world().entityFromSlotIndex(slot);
    if (e != InvalidEntity && engine.world().isAlive(e)) {
      auto &world = engine.world();

      // Compute view/proj matching engine camera
      auto &cam = m_persist.camera;

      const uint32_t w = std::max(1u, m_viewport.desiredSize.x);
      const uint32_t h = std::max(1u, m_viewport.desiredSize.y);

      cam.setViewport(w, h);
      cam.updateIfDirty();

      // ImGuizmo setup
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetDrawlist();

      ImGuizmo::SetRect(m_viewport.imageMin.x, m_viewport.imageMin.y,
                        (m_viewport.imageMax.x - m_viewport.imageMin.x),
                        (m_viewport.imageMax.y - m_viewport.imageMin.y));

      // We manipulate WORLD matrix for best experience.
      // CWorldTransform.world)
      world.updateTransforms(); // ensure up-to-date before gizmo

      glm::mat4 worldM = world.worldTransform(e).world;
      const glm::mat4 prevWorldM = worldM;

      // Snap values
      float snap[3] = {0, 0, 0};
      const bool doSnap = m_gizmo.useSnap;
      if (doSnap) {
        if (m_gizmo.op == GizmoOp::Translate) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapTranslate;
        } else if (m_gizmo.op == GizmoOp::Rotate) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapRotateDeg;
        } else if (m_gizmo.op == GizmoOp::Scale) {
          snap[0] = snap[1] = snap[2] = m_gizmo.snapScale;
        }
      }

      // Manipulate
      ImGuizmo::OPERATION op = toImGuizmoOp(m_gizmo.op);
      ImGuizmo::MODE mode = toImGuizmoMode(m_gizmo.mode);

      // Allow gizmo input only when the mouse is over the viewport image.
      ImGuizmo::Enable(true);
      ImGuizmo::Manipulate(
          glm::value_ptr(cam.view()), glm::value_ptr(cam.proj()), op, mode,
          glm::value_ptr(worldM), nullptr, doSnap ? snap : nullptr);

      m_gizmoUsing = ImGuizmo::IsUsing();
      m_gizmoOver = ImGuizmo::IsOver();

      if (m_gizmoUsing) {
        // Convert worldM back into LOCAL transform (preserve parenting)
        // We need parent world matrix inverse
        EntityID p = world.parentOf(e);
        glm::mat4 parentW(1.0f);
        if (p != InvalidEntity) {
          parentW = world.worldTransform(p).world;
        }
        glm::mat4 localM = glm::inverse(parentW) * worldM;

        // Decompose localM into TRS and write to CTransform
        glm::vec3 t, s, skew;
        glm::vec4 persp;
        glm::quat r;
        glm::decompose(localM, s, r, t, skew, persp);
        if (s.x <= 0.01f)
          s.x = 0.01f;
        if (s.y <= 0.01f)
          s.y = 0.01f;
        if (s.z <= 0.01f)
          s.z = 0.01f;

        auto &tr = world.transform(e);
        tr.translation = t;
        tr.rotation = r;
        tr.scale = s;
        tr.dirty = true;

        if (sel.picks.size() > 1) {
          const glm::mat4 delta = worldM * glm::inverse(prevWorldM);
          std::unordered_set<EntityID> selected;
          selected.reserve(sel.picks.size());
          std::vector<EntityID> entities;
          entities.reserve(sel.picks.size());
          for (uint32_t pick : sel.picks) {
            EntityID pe = world.entityFromSlotIndex(pickEntity(pick));
            if (pe == InvalidEntity || !world.isAlive(pe))
              continue;
            if (selected.insert(pe).second)
              entities.push_back(pe);
          }

          for (EntityID pe : entities) {
            if (pe == e)
              continue;
            glm::mat4 otherWorld = world.worldTransform(pe).world;
            glm::mat4 newWorld = delta * otherWorld;

            EntityID parent = world.parentOf(pe);
            glm::mat4 parentW(1.0f);
            if (parent != InvalidEntity) {
              parentW = world.worldTransform(parent).world;
              if (selected.find(parent) != selected.end())
                parentW = delta * parentW;
            }
            glm::mat4 localOther = glm::inverse(parentW) * newWorld;

            glm::vec3 ot, os, oskew;
            glm::vec4 opersp;
            glm::quat orot;
            glm::decompose(localOther, os, orot, ot, oskew, opersp);
            if (os.x <= 0.01f)
              os.x = 0.01f;
            if (os.y <= 0.01f)
              os.y = 0.01f;
            if (os.z <= 0.01f)
              os.z = 0.01f;

            auto &otr = world.transform(pe);
            otr.translation = ot;
            otr.rotation = orot;
            otr.scale = os;
            otr.dirty = true;
          }
        }
      }
    }
  }

  ImGui::End();
}

void EditorLayer::drawStats(EngineContext &engine) {
  ImGui::Begin("Stats");
  ImGui::Text("dt: %.3f ms", engine.dt() * 1000.0f);
  ImGui::Text("Window: %d x %d", m_persist.camera.viewportW,
              m_persist.camera.viewportH);
  ImGui::Text("Last Pick: 0x%08X", engine.lastPickedID());
  const char *viewModeNames[] = {
      "Lit", "Albedo", "Normals", "Roughness", "Metallic", "AO", "Depth", "ID",
  };
  int vmIdx = static_cast<int>(engine.viewMode());
  ImGui::Combo("View Mode", &vmIdx, viewModeNames, IM_ARRAYSIZE(viewModeNames));
  engine.setViewMode(static_cast<ViewMode>(vmIdx));
  ImGui::End();
}

void EditorLayer::onImGui(EngineContext &engine) {
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("Window")) {
      if (ImGui::MenuItem("Reset Layout")) {
        m_persist.dockLayoutApplied = false; // allow rebuild
        const ImGuiViewport *vp = ImGui::GetMainViewport();
        BuildDefaultDockLayout(engine.dockspaceID(), vp->WorkSize);
      }
      ImGui::MenuItem("Viewport", nullptr, &m_persist.panels.viewport);
      ImGui::MenuItem("Hierarchy", nullptr, &m_persist.panels.hierarchy);
      ImGui::MenuItem("Inspector", nullptr, &m_persist.panels.inspector);
      ImGui::MenuItem("Stats", nullptr, &m_persist.panels.stats);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }

  if (!m_world) {
    ImGui::Begin("Hierarchy");
    ImGui::TextUnformatted("No world loaded");
    ImGui::End();
    return;
  }

  // Viewport panel
  if (m_persist.panels.viewport)
    drawViewport(engine);

  // Stats panel
  if (m_persist.panels.stats)
    drawStats(engine);

  // Gizmo toolbar
  drawGizmoToolbar(m_gizmo);

  // Hierarchy panel
  if (m_persist.panels.hierarchy)
    m_hierarchy.draw(*m_world, m_sel);

  // Add menu (Shift+A)
  const bool allowOpen = !ImGui::GetIO().WantTextInput;
  m_add.tick(*m_world, m_sel, allowOpen);

  // Inspector panel
  if (m_persist.panels.inspector)
    m_inspector.draw(*m_world, engine, m_sel);

  // Asset Browser panel
  if (m_persist.panels.assetBrowser) {
    ImGui::Begin("Asset Browser");
    ImGui::TextUnformatted("Asset Browser not yet implemented");
    ImGui::End();
  }
}

} // namespace Nyx
