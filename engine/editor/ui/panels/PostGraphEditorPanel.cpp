#include "PostGraphEditorPanel.h"

#include "app/EngineContext.h"
#include "core/Log.h"
#include "editor/graph/GraphEditorInfra.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Nyx {

PostGraphEditorPanel::PostGraphEditorPanel() {
  m_ctx = GraphEditorInfra::createNodeEditorContext(
      ".cache/post_graph_editor_settings.ini");
}

PostGraphEditorPanel::~PostGraphEditorPanel() {
  GraphEditorInfra::destroyNodeEditorContext(m_ctx);
}

void PostGraphEditorPanel::draw(PostGraph &graph,
                                const FilterRegistry &registry,
                                EngineContext &engine) {
  const auto drawStart = std::chrono::steady_clock::now();
  ImGui::Begin("Post Graph");
  m_isHovered = GraphEditorInfra::graphWindowWantsPriority();
  if (m_isHovered)
    engine.requestUiBlockGlobalShortcuts();
  if (ImGui::Button("Auto Layout"))
    m_requestAutoLayout = true;

  ImGui::SameLine();
  if (ImGui::Button("Zoom to Fit"))
    m_requestNavigateToContent = true;

  ImGui::SameLine();
  {
    const char *presets[] = {"Custom",   "Filmic",  "Cinematic",
                             "Arcade",   "Natural", "Noir",
                             "Warm",     "Cool",    "Vibrant"};
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("##post_preset", &m_presetIndex, presets,
                     (int)(sizeof(presets) / sizeof(presets[0])))) {
      if (m_presetIndex > 0) {
        applyPreset(graph, registry, m_presetIndex);
        m_requestNavigateToContent = true;
        markChanged();
      }
    }
  }
  ImGui::SameLine();
  ImGui::TextDisabled("CPU %.2f ms", m_lastDrawMs);

  ed::SetCurrentEditor(m_ctx);
  auto &style = ed::GetStyle();
  style.FlowDuration = 0.5f;

  ed::Begin("PostGraphCanvas");
  const bool ctrlDown = ImGui::GetIO().KeyCtrl;
  if (!m_ctrlDragActive && ctrlDown &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ed::NodeId hovered = ed::GetHoveredNode();
    if (hovered.AsPointer() != nullptr) {
      const PGNodeID nid =
          static_cast<PGNodeID>(reinterpret_cast<uintptr_t>(hovered.AsPointer()));
      if (nid != graph.inputNode() && nid != graph.outputNode()) {
        m_ctrlDragActive = true;
        m_ctrlDragNodeId = nid;

        PGNode *n = graph.findNode(nid);
        PGPinID prevOut = 0;
        PGPinID nextIn = 0;
        if (n && n->inPin != 0 && n->outPin != 0) {
          for (const auto &l : graph.links()) {
            if (l.toPin == n->inPin)
              prevOut = l.fromPin;
            if (l.fromPin == n->outPin)
              nextIn = l.toPin;
          }
        }

        unlinkNode(graph, nid);

        if (prevOut != 0 && nextIn != 0 && prevOut != nextIn) {
          PGCompileError err{};
          if (!graph.tryAddLink(prevOut, nextIn, &err)) {
            Log::Warn("PostGraphEditorPanel: Failed to re-link node after ctrl-drag unlink: {}",
                      err.message);
          }
        }

        markChanged();
      }
    }
  }

  if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X))
    onDeleteSelection(graph);
  if (ImGui::IsKeyPressed(ImGuiKey_U))
    onUnlinkSelection(graph);

  m_pinScreenCache.clear();
  const size_t targetPins = graph.nodes().size() * 2 + 2;
  if (m_pinScreenCache.bucket_count() < targetPins)
    m_pinScreenCache.reserve(targetPins);

  for (auto &n : const_cast<std::vector<PGNode> &>(graph.nodes())) {
    ed::BeginNode(ed::NodeId(static_cast<uintptr_t>(n.id)));
    ImGui::TextUnformatted(n.name.c_str());
    ImGui::Separator();

    ImGui::BeginGroup();
    if (n.inPin != 0) {
      ed::BeginPin(ed::PinId(static_cast<uintptr_t>(n.inPin)),
                   ed::PinKind::Input);
      ImGui::TextUnformatted("In");
      const ImVec2 pinMin = ImGui::GetItemRectMin();
      const ImVec2 pinMax = ImGui::GetItemRectMax();
      ed::PinRect(pinMin, pinMax);
      m_pinScreenCache[n.inPin] = {ImVec2((pinMin.x + pinMax.x) * 0.5f,
                                          (pinMin.y + pinMax.y) * 0.5f),
                                   false};
      ed::EndPin();
    } else {
      ImGui::TextUnformatted(" ");
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    if (n.outPin != 0) {
      ed::BeginPin(ed::PinId(static_cast<uintptr_t>(n.outPin)),
                   ed::PinKind::Output);
      ImGui::TextUnformatted("Out");
      const ImVec2 pinMin = ImGui::GetItemRectMin();
      const ImVec2 pinMax = ImGui::GetItemRectMax();
      ed::PinRect(pinMin, pinMax);
      m_pinScreenCache[n.outPin] = {ImVec2((pinMin.x + pinMax.x) * 0.5f,
                                           (pinMin.y + pinMax.y) * 0.5f),
                                    true};
      ed::EndPin();
    } else {
      ImGui::TextUnformatted(" ");
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    drawNodeContents(graph, registry, engine, n);
    ed::EndNode();

    if (m_initializedNodes.find(n.id) == m_initializedNodes.end()) {
      if (n.posX != 0.0f || n.posY != 0.0f) {
        ed::SetNodePosition(ed::NodeId(static_cast<uintptr_t>(n.id)),
                            ImVec2(n.posX, n.posY));
      }
      m_initializedNodes.insert(n.id);
    }

    const ImVec2 pos =
        ed::GetNodePosition(ed::NodeId(static_cast<uintptr_t>(n.id)));
    n.posX = pos.x;
    n.posY = pos.y;
  }

  for (const auto &l : graph.links()) {
    ed::Link(ed::LinkId(static_cast<uintptr_t>(l.id)),
             ed::PinId(static_cast<uintptr_t>(l.fromPin)),
             ed::PinId(static_cast<uintptr_t>(l.toPin)));
  }

  if (m_ctrlDragActive && m_ctrlDragNodeId != 0) {
    ed::EnableShortcuts(false);
    PGLinkID hoveredLinkId = 0;
    const ImVec2 mouseScreen = ImGui::GetMousePos();
    const ed::LinkId hoveredLink = ed::GetHoveredLink();
    if (hoveredLink.AsPointer() != nullptr) {
      hoveredLinkId = static_cast<PGLinkID>(
          reinterpret_cast<uintptr_t>(hoveredLink.AsPointer()));
    }

    auto pinNodePos = [&](PGPinID pin, ImVec2 &posOut, bool &isOutput) -> bool {
      const auto it = m_pinScreenCache.find(pin);
      if (it == m_pinScreenCache.end())
        return false;
      posOut = it->second.pos;
      isOutput = it->second.isOutput;
      return true;
    };

    auto distToSegmentSq = [](const ImVec2 &p, const ImVec2 &a,
                              const ImVec2 &b) -> float {
      const float vx = b.x - a.x;
      const float vy = b.y - a.y;
      const float wx = p.x - a.x;
      const float wy = p.y - a.y;
      const float vv = vx * vx + vy * vy;
      float t = 0.0f;
      if (vv > 1e-5f)
        t = (wx * vx + wy * vy) / vv;
      t = std::clamp(t, 0.0f, 1.0f);
      const float cx = a.x + t * vx;
      const float cy = a.y + t * vy;
      const float dx = p.x - cx;
      const float dy = p.y - cy;
      return dx * dx + dy * dy;
    };

    auto distToBezierSq = [&](const ImVec2 &p, const ImVec2 &a,
                              const ImVec2 &b) -> float {
      const float dx = b.x - a.x;
      const float tlen = std::max(40.0f, std::abs(dx) * 0.5f);
      const ImVec2 p1(a.x + tlen, a.y);
      const ImVec2 p2(b.x - tlen, b.y);

      auto bezierPoint = [](const ImVec2 &p0, const ImVec2 &p1,
                            const ImVec2 &p2, const ImVec2 &p3,
                            float t) -> ImVec2 {
        const float u = 1.0f - t;
        const float tt = t * t;
        const float uu = u * u;
        const float uuu = uu * u;
        const float ttt = tt * t;
        ImVec2 r(0.0f, 0.0f);
        r.x = uuu * p0.x + 3.0f * uu * t * p1.x + 3.0f * u * tt * p2.x +
              ttt * p3.x;
        r.y = uuu * p0.y + 3.0f * uu * t * p1.y + 3.0f * u * tt * p2.y +
              ttt * p3.y;
        return r;
      };

      constexpr int kSegments = 16;
      float best = FLT_MAX;
      ImVec2 prev = a;
      for (int s = 1; s <= kSegments; ++s) {
        const float t = (float)s / (float)kSegments;
        const ImVec2 cur = bezierPoint(a, p1, p2, b, t);
        const float d = distToSegmentSq(p, prev, cur);
        if (d < best)
          best = d;
        prev = cur;
      }
      return best;
    };

    if (hoveredLinkId == 0) {
      float bestDist = FLT_MAX;
      for (const auto &l : graph.links()) {
        ImVec2 a, b;
        bool aOut = false, bOut = false;
        if (!pinNodePos(l.fromPin, a, aOut) || !pinNodePos(l.toPin, b, bOut))
          continue;

        const float d = distToBezierSq(mouseScreen, a, b);
        if (d < bestDist) {
          bestDist = d;
          hoveredLinkId = l.id;
        }
      }

      if (bestDist >= 1600.0f)
        hoveredLinkId = 0;
    }

    if (hoveredLinkId != 0) {
      ed::Flow(ed::LinkId(static_cast<uintptr_t>(hoveredLinkId)));
      ed::Suspend();
      ImGui::SetTooltip("Insert into link");
      ed::Resume();
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      if (hoveredLinkId != 0)
        tryInsertNodeIntoLink(graph, m_ctrlDragNodeId, hoveredLinkId);
      m_ctrlDragActive = false;
      m_ctrlDragNodeId = 0;
    }
  } else {
    ed::EnableShortcuts(true);
  }

  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      if (a && b) {
        const PGPinID pa = reinterpret_cast<uintptr_t>(a.AsPointer());
        const PGPinID pb = reinterpret_cast<uintptr_t>(b.AsPointer());

        PGCompileError err;
        if (graph.tryAddLink(pa, pb, &err) || graph.tryAddLink(pb, pa, &err)) {
          ed::AcceptNewItem();
          markChanged();
        } else {
          ed::RejectNewItem();
          if (!err.message.empty()) {
            ed::Suspend();
            ImGui::SetTooltip("%s", err.message.c_str());
            ed::Resume();
          }
        }
      }
    }
  }
  ed::EndCreate();

  if (m_requestAutoLayout) {
    autoLayout(graph);
    m_requestAutoLayout = false;
    m_requestNavigateToContent = true;
  }
  if (m_requestNavigateToContent) {
    ed::NavigateToContent(0.0f);
    m_requestNavigateToContent = false;
  } else if (m_initialZoomPending) {
    if (!m_initialZoomArmed) {
      m_initialZoomArmed = true;
    } else {
      ed::NavigateToContent(0.0f);
      m_initialZoomPending = false;
      m_initialZoomArmed = false;
    }
  }

  ed::End();
  ed::SetCurrentEditor(nullptr);

  GraphEditorInfra::PopupState popup{
      m_openAddMenu, m_requestOpenAddMenu, m_popupPos};
  GraphEditorInfra::triggerAddMenuAtMouse(m_isHovered, popup, m_search,
                                          sizeof(m_search));
  m_openAddMenu = popup.open;
  m_requestOpenAddMenu = popup.requestOpen;
  m_popupPos = popup.popupPos;

  if (m_openAddMenu || m_requestOpenAddMenu)
    drawAddMenu(graph, registry);

  ImGui::End();
  const auto drawEnd = std::chrono::steady_clock::now();
  m_lastDrawMs =
      std::chrono::duration<float, std::milli>(drawEnd - drawStart).count();
}

} // namespace Nyx
