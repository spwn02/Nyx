#include "PostGraphEditorPanel.h"
#include "app/EngineContext.h"
#include "post/FilterRegistry.h"
#include "post/PostGraphTypes.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"

#include "core/Log.h"

#include <cstdint>
#include <algorithm>
#include <cfloat>
#include <imgui.h>

#include <imgui_node_editor.h>
#include <vector>
#include <unordered_map>

namespace ed = ax::NodeEditor;

namespace Nyx {

static bool passFilterCI(const char *filter, const char *text) {
  if (!filter || filter[0] == 0)
    return true;
  auto lower = [](char c) -> char {
    return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
  };
  for (const char *p = text; *p; ++p) {
    const char *a = p;
    const char *b = filter;
    while (*a && *b && lower(*a) == lower(*b)) {
      ++a;
      ++b;
    }
    if (*b == 0)
      return true;
  }
  return false;
}

static const char *filenameOnly(const std::string &path) {
  if (path.empty())
    return "Identity";
  const size_t a = path.find_last_of("/\\");
  return (a == std::string::npos) ? path.c_str() : path.c_str() + a + 1;
}

static bool isCubeFile(const std::string &path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos)
    return false;
  std::string ext = path.substr(dot + 1);
  for (char &c : ext)
    c = (char)std::tolower((unsigned char)c);
  return ext == "cube";
}

PostGraphEditorPanel::PostGraphEditorPanel() {
  ed::Config cfg;
  cfg.SettingsFile = ".nyx/post_graph_editor_settings.ini";
  m_ctx = ed::CreateEditor(&cfg);
}

PostGraphEditorPanel::~PostGraphEditorPanel() {
  if (m_ctx) {
    ed::DestroyEditor(m_ctx);
    m_ctx = nullptr;
  }
}

void PostGraphEditorPanel::draw(PostGraph &graph,
                                const FilterRegistry &registry,
                                EngineContext &engine) {
  ImGui::Begin("Post Graph");
  m_isHovered = ImGui::IsWindowHovered(
      ImGuiHoveredFlags_AllowWhenBlockedByPopup |
      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (m_isHovered)
    engine.requestUiBlockGlobalShortcuts();
  if (ImGui::Button("Auto Layout")) {
    m_requestAutoLayout = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Zoom to Fit")) {
    m_requestNavigateToContent = true;
  }
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

  ed::SetCurrentEditor(m_ctx);

  auto &style = ed::GetStyle();
  style.FlowDuration = 0.5f;

  ed::Begin("PostGraphCanvas");

  // Shift+A add menu when hovered (handled after node editor)
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

        if (prevOut != 0 && nextIn != 0) {
          if (prevOut != 0 && nextIn != 0 && prevOut != nextIn) {
            PGCompileError err{};
            if (!graph.tryAddLink(prevOut, nextIn, &err)) {
              Log::Warn(
                  "PostGraphEditorPanel: Failed to re-link node after ctrl-drag unlink: {}",
                  err.message);
            }
          }
        }

        markChanged();
      }
    }
  }

  // Delete selection (Del / X)
  if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X)) {
    onDeleteSelection(graph);
  }
  // Unlink selection (U)
  if (ImGui::IsKeyPressed(ImGuiKey_U)) {
    onUnlinkSelection(graph);
  }

  struct PinScreenData {
    ImVec2 pos;
    bool isOutput = false;
  };
  std::unordered_map<PGPinID, PinScreenData> pinScreen;
  pinScreen.reserve(graph.nodes().size() * 2 + 2);

  // Draw nodes
  for (auto &n : const_cast<std::vector<PGNode> &>(graph.nodes())) {
    ed::BeginNode(ed::NodeId(static_cast<uintptr_t>(n.id)));

    // Title
    ImGui::TextUnformatted(n.name.c_str());
    ImGui::Separator();

    // Pins row (left input, right output)
    ImGui::BeginGroup();
    if (n.inPin != 0) {
      ed::BeginPin(ed::PinId(static_cast<uintptr_t>(n.inPin)),
                   ed::PinKind::Input);
      ImGui::TextUnformatted("In");
      const ImVec2 pinMin = ImGui::GetItemRectMin();
      const ImVec2 pinMax = ImGui::GetItemRectMax();
      ed::PinRect(pinMin, pinMax);
      pinScreen[n.inPin] = {ImVec2((pinMin.x + pinMax.x) * 0.5f,
                                   (pinMin.y + pinMax.y) * 0.5f),
                            false};
      ed::EndPin();
    } else {
      ImGui::TextUnformatted(" ");
    }

    ImGui::EndGroup();

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
      if (n.outPin != 0) {
        ed::BeginPin(ed::PinId(static_cast<uintptr_t>(n.outPin)),
                     ed::PinKind::Output);
        ImGui::TextUnformatted("Out");
        const ImVec2 pinMin = ImGui::GetItemRectMin();
        const ImVec2 pinMax = ImGui::GetItemRectMax();
        ed::PinRect(pinMin, pinMax);
        pinScreen[n.outPin] = {ImVec2((pinMin.x + pinMax.x) * 0.5f,
                                      (pinMin.y + pinMax.y) * 0.5f),
                               true};
        ed::EndPin();
      } else {
        ImGui::TextUnformatted(" ");
      }
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // Node body (node-only UX)
    drawNodeContents(graph, registry, engine, n);

    ed::EndNode();

    // Initialize position once, then let the editor own movement.
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

  // Draw links
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
      const auto it = pinScreen.find(pin);
      if (it == pinScreen.end())
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
      if (t < 0.0f)
        t = 0.0f;
      if (t > 1.0f)
        t = 1.0f;
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

        const ImVec2 as = a;
        const ImVec2 bs = b;
        const float d = distToBezierSq(mouseScreen, as, bs);
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
    } else {
      hoveredLinkId = 0;
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
      if (hoveredLinkId != 0) {
        tryInsertNodeIntoLink(graph, m_ctrlDragNodeId, hoveredLinkId);
      }
      m_ctrlDragActive = false;
      m_ctrlDragNodeId = 0;
    }
  } else {
    ed::EnableShortcuts(true);
  }

  // Link creation
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      if (a && b) {
        const PGPinID pa = reinterpret_cast<uintptr_t>(a.AsPointer());
        const PGPinID pb = reinterpret_cast<uintptr_t>(b.AsPointer());

        PGCompileError err;
        if (graph.tryAddLink(pa, pb, &err)) {
          ed::AcceptNewItem();
          markChanged();
        } else if (graph.tryAddLink(pb, pa, &err)) {
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
    // Wait one frame so node sizes/positions are available, then fit.
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

  // Shift+A add menu when panel is hovered (screen-space)
  if (m_isHovered) {
    if (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_A)) {
      m_openAddMenu = true;
      m_requestOpenAddMenu = true;
      m_search[0] = 0;

      const ImVec2 mp = ImGui::GetMousePos();
      m_popupX = mp.x;
      m_popupY = mp.y;
    }
  }

  drawAddMenu(graph, registry);

  ImGui::End();
}

void PostGraphEditorPanel::drawNodeContents(PostGraph &graph,
                                            const FilterRegistry &registry,
                                            EngineContext &engine, PGNode &n) {
  ImGui::PushID(static_cast<int>(n.id));
  if (n.kind == PGNodeKind::Input) {
    ImGui::TextUnformatted("Scene HDR in");
    ImGui::PopID();
    return;
  }
  if (n.kind == PGNodeKind::Output) {
    ImGui::TextUnformatted("Final LDR out");
    ImGui::PopID();
    return;
  }

  const FilterTypeInfo *t = registry.find(static_cast<FilterTypeId>(n.typeID));

  // Enabled toggle
  bool en = n.enabled;
  if (ImGui::Checkbox("##enabled", &en)) {
    n.enabled = en;
    markChanged();
  }
  ImGui::SameLine();
  ImGui::TextUnformatted(t ? t->name : "Filter");

  // Buttons row: Reset / Copy / Paste
  ImGui::Spacing();
  if (ImGui::Button("Reset")) {
    if (t) {
      // Reset to registry defaults
      n.params.clear();
      n.params.reserve(t->paramCount);
      for (uint32_t i = 0; i < t->paramCount; ++i)
        n.params.push_back(t->params[i].defaultValue);
      markChanged();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    m_clipTypeID = n.typeID;
    m_clipParams = n.params;
  }
  ImGui::SameLine();
  const bool canPaste = (m_clipTypeID == n.typeID) && (!m_clipParams.empty());
  if (!canPaste)
    ImGui::BeginDisabled();
  if (ImGui::Button("Paste")) {
    n.params = m_clipParams;
    markChanged();
  }
  if (!canPaste)
    ImGui::EndDisabled();

  // Params inside node
  ImGui::Spacing();
  if (t && std::string(t->name) == "LUT") {
    if (n.params.size() < 2)
      n.params.resize(2, 0.0f);
    float intensity = n.params[0];
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f)) {
      n.params[0] = intensity;
      markChanged();
    }

    const auto &lutPaths = engine.postLUTPaths();
    int currentIndex = 0;
    if (!n.lutPath.empty()) {
      for (size_t i = 0; i < lutPaths.size(); ++i) {
        if (lutPaths[i] == n.lutPath) {
          currentIndex = (int)i;
          break;
        }
      }
    }

    ImGui::TextUnformatted("LUT");
    ImGui::SameLine();
    const char *preview =
        (n.lutPath.empty() ? "Identity" : filenameOnly(n.lutPath));
    if (ImGui::BeginCombo("##lut_combo", preview)) {
      for (size_t i = 0; i < lutPaths.size(); ++i) {
        const bool selected = (int)i == currentIndex;
        const char *label = filenameOnly(lutPaths[i]);
        if (ImGui::Selectable(label, selected)) {
          n.lutPath = lutPaths[i];
          markChanged();
        }
        if (selected)
          ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    const char *btn = n.lutPath.empty() ? "Select..." : "Change...";
    if (ImGui::Button(btn)) {
      const char *filters = "cube";
      if (auto path = FileDialogs::openFile("Select LUT", filters, nullptr)) {
        if (!path->empty()) {
          n.lutPath = *path;
          markChanged();
        }
      }
    }

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
        const char *p = (const char *)payload->Data;
        if (p) {
          std::string path(p);
          if (isCubeFile(path)) {
            n.lutPath = path;
            markChanged();
          }
        }
      }
      ImGui::EndDragDropTarget();
    }

    if (!n.lutPath.empty()) {
      ImGui::TextUnformatted(n.lutPath.c_str());
    }
  } else if (t) {
    const size_t want = t->paramCount;
    if (n.params.size() != want) {
      // Keep stable shape (don't mutate silently beyond resizing)
      n.params.resize(want, 0.0f);
    }

    for (size_t i = 0; i < want; ++i) {
      const auto &pd = t->params[i];
      float v = n.params[i];

      ImGui::PushID(static_cast<int>(i));
      ImGui::SetNextItemWidth(160.0f);
      bool edited = false;

      if (n.typeID == 29u && std::strcmp(pd.name, "Wrap Mode") == 0) {
        int mode = (v < 0.5f) ? 0 : (v < 1.5f ? 1 : 2);
        ImGui::TextUnformatted(pd.name);
        if (ImGui::RadioButton("Clamp", mode == 0)) {
          mode = 0;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Repeat", mode == 1)) {
          mode = 1;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Mirror", mode == 2)) {
          mode = 2;
          n.params[i] = static_cast<float>(mode);
          edited = true;
        }
        if (edited) {
          m_lastEditCommit = ImGui::GetTime();
          markChanged();
          edited = false;
        }
      } else {
      switch (pd.ui) {
      case FilterParamUI::Slider:
        if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
          n.params[i] = v;
          edited = true;
        }
        break;
      case FilterParamUI::Drag:
        if (ImGui::DragFloat(pd.name, &v, pd.step, pd.minValue, pd.maxValue)) {
          n.params[i] = v;
          edited = true;
        }
        break;
      case FilterParamUI::Checkbox: {
        bool b = v > 0.5f;
        if (ImGui::Checkbox(pd.name, &b)) {
          n.params[i] = b ? 1.0f : 0.0f;
          markChanged();
        }
      } break;
      case FilterParamUI::Color3: {
        if (i + 2 < want) {
          float col[3] = {n.params[i + 0], n.params[i + 1], n.params[i + 2]};
          if (ImGui::ColorEdit3(pd.name, col)) {
            n.params[i + 0] = col[0];
            n.params[i + 1] = col[1];
            n.params[i + 2] = col[2];
            edited = true;
          }
          i += 2;
        } else if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
          n.params[i] = v;
          edited = true;
        }
      } break;
      case FilterParamUI::Color4: {
        if (i + 3 < want) {
          float col[4] = {n.params[i + 0], n.params[i + 1], n.params[i + 2],
                          n.params[i + 3]};
          if (ImGui::ColorEdit4(pd.name, col)) {
            n.params[i + 0] = col[0];
            n.params[i + 1] = col[1];
            n.params[i + 2] = col[2];
            n.params[i + 3] = col[3];
            edited = true;
          }
          i += 3;
        } else if (ImGui::SliderFloat(pd.name, &v, pd.minValue, pd.maxValue)) {
          n.params[i] = v;
          edited = true;
        }
      } break;
      }
      }
      if (edited) {
        const double now = ImGui::GetTime();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          m_lastEditCommit = now;
          markChanged();
        } else if (ImGui::IsItemActive() &&
                   (now - m_lastEditCommit) > 0.08) {
          m_lastEditCommit = now;
          markChanged();
        }
      }
      ImGui::PopID();
    }
  } else {
    ImGui::TextUnformatted("(unknown filter type)");
  }
  ImGui::PopID();
}

void PostGraphEditorPanel::drawAddMenu(PostGraph &graph,
                                       const FilterRegistry &registry) {
  if (m_requestOpenAddMenu) {
    ImGui::SetNextWindowPos(ImVec2(m_popupX, m_popupY), ImGuiCond_Appearing);
    ImGui::OpenPopup("AddFilterNode");
    m_requestOpenAddMenu = false;
  } else {
    ImGui::SetNextWindowPos(ImVec2(m_popupX, m_popupY), ImGuiCond_Appearing);
  }

  if (!ImGui::BeginPopup("AddFilterNode", ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextUnformatted("Add Filter");
  ImGui::Separator();

  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  ImGui::InputTextWithHint("##search", "Search filters...", m_search,
                           sizeof(m_search));

  ImGui::Separator();

  // Group by category
  const auto &all = registry.types();
  const bool filterActive = m_search[0] != 0;

  // Collect unique categories in stable order
  std::vector<const char *> cats;
  cats.reserve(all.size());
  for (const auto &t : all) {
    bool seen = false;
    for (auto *c : cats) {
      if (std::string(c) == std::string(t.category)) {
        seen = true;
        break;
      }
    }
    if (!seen)
      cats.push_back(t.category);
  }

  for (const char *cat : cats) {
    bool anyInCat = false;
    for (const auto &t : all) {
      if (std::string(t.category) != std::string(cat))
        continue;
      if (!passFilterCI(m_search, t.name))
        continue;
      anyInCat = true;
      break;
    }
    if (!anyInCat)
      continue;

    if (filterActive) {
      ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }
    if (ImGui::TreeNode(cat)) {
      for (const auto &t : all) {
        if (std::string(t.category) != std::string(cat))
          continue;
        if (!passFilterCI(m_search, t.name))
          continue;

        if (ImGui::Selectable(t.name)) {
          std::vector<float> defaults;
          defaults.reserve(t.paramCount);
          for (uint32_t i = 0; i < t.paramCount; ++i)
            defaults.push_back(t.params[i].defaultValue);

          const char *label =
              (t.defaultLabel && t.defaultLabel[0]) ? t.defaultLabel : t.name;
          graph.addFilter(static_cast<uint32_t>(t.id), label, defaults);
          markChanged();

          ImGui::CloseCurrentPopup();
          m_openAddMenu = false;
        }
      }
      ImGui::TreePop();
    }
  }

  ImGui::EndPopup();
}

void PostGraphEditorPanel::autoLayout(PostGraph &graph) {
  std::vector<PGNodeID> order;
  const PGCompileError err = graph.buildChainOrder(order);
  if (!err.ok)
    return;

  std::vector<PGNodeID> nodes;
  nodes.reserve(order.size() + 2);
  nodes.push_back(graph.inputNode());
  for (PGNodeID id : order)
    nodes.push_back(id);
  nodes.push_back(graph.outputNode());

  float x = 0.0f;
  const float y = 0.0f;
  const float xSpacing = 120.0f;

  for (PGNodeID id : nodes) {
    const ed::NodeId nid(static_cast<uintptr_t>(id));
    const ImVec2 size = ed::GetNodeSize(nid);
    ed::SetNodePosition(nid, ImVec2(x, y));

    if (PGNode *n = graph.findNode(id)) {
      n->posX = x;
      n->posY = y;
    }

    x += size.x + xSpacing;
  }
}

void PostGraphEditorPanel::applyPreset(PostGraph &graph,
                                       const FilterRegistry &registry,
                                       int presetIndex) {
  graph = PostGraph();

  auto add = [&](const char *name, const std::vector<float> *overrideParams) {
    const FilterTypeInfo *t = registry.findByName(name);
    if (!t)
      return;
    std::vector<float> defaults;
    defaults.reserve(t->paramCount);
    for (uint32_t i = 0; i < t->paramCount; ++i)
      defaults.push_back(t->params[i].defaultValue);
    PGNodeID id =
        graph.addFilter((uint32_t)t->id, t->defaultLabel && t->defaultLabel[0]
                                         ? t->defaultLabel
                                         : t->name,
                        defaults);
    if (overrideParams) {
      if (PGNode *n = graph.findNode(id)) {
        n->params = *overrideParams;
      }
    }
  };

  switch (presetIndex) {
  case 1: // Filmic
    add("Exposure", nullptr);
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    add("Vignette", nullptr);
    break;
  case 2: // Cinematic
    add("Exposure", nullptr);
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    add("Vignette", nullptr);
    {
      const std::vector<float> lens = {-0.15f, 1.0f, 0.003f};
      add("Lens Distortion", &lens);
    }
    {
      const std::vector<float> ca = {0.003f, 1.2f};
      add("Chromatic Aberration", &ca);
    }
    break;
  case 3: // Arcade
    {
      const std::vector<float> sat = {1.4f};
      add("Saturation", &sat);
    }
    {
      const std::vector<float> con = {1.2f};
      add("Contrast", &con);
    }
    {
      const std::vector<float> glitch = {0.35f, 32.0f, 1.5f, 1.0f};
      add("Glitch", &glitch);
    }
    {
      const std::vector<float> ca = {0.004f, 1.4f};
      add("Chromatic Aberration", &ca);
    }
    {
      const std::vector<float> sharp = {0.35f, 1.0f};
      add("Sharpen", &sharp);
    }
    break;
  case 4: // Natural
    add("Exposure", nullptr);
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 5: // Noir
    {
      const std::vector<float> con = {1.3f};
      add("Contrast", &con);
    }
    add("Grayscale", nullptr);
    {
      const std::vector<float> vig = {0.45f, 0.65f, 0.35f};
      add("Vignette", &vig);
    }
    break;
  case 6: // Warm
    {
      const std::vector<float> tint = {0.35f, 1.05f, 0.92f, 0.85f};
      add("Tint", &tint);
    }
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 7: // Cool
    {
      const std::vector<float> tint = {0.35f, 0.85f, 0.95f, 1.05f};
      add("Tint", &tint);
    }
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 8: // Vibrant
    {
      const std::vector<float> con = {1.2f};
      add("Contrast", &con);
    }
    {
      const std::vector<float> sat = {1.35f};
      add("Saturation", &sat);
    }
    {
      const std::vector<float> sharp = {0.3f, 1.0f};
      add("Sharpen", &sharp);
    }
    break;
  default:
    break;
  }
}

void PostGraphEditorPanel::onDeleteSelection(PostGraph &graph) {
  ed::SetCurrentEditor(m_ctx);

  // Delete selected links first
  {
    std::vector<ed::LinkId> links;
    links.resize(ed::GetSelectedObjectCount());
    int linkCount = ed::GetSelectedLinks(links.data(), links.size());
    for (int i = 0; i < linkCount; ++i) {
      PGLinkID lid = static_cast<PGLinkID>(
          reinterpret_cast<uintptr_t>(links[i].AsPointer()));
      graph.removeLink(lid);
      markChanged();
    }
  }

  // Delete selected nodes
  {
    std::vector<ed::NodeId> nodes;
    nodes.resize(ed::GetSelectedObjectCount());
    int nodeCount = ed::GetSelectedNodes(nodes.data(), nodes.size());
    for (int i = 0; i < nodeCount; ++i) {
      PGNodeID nid = static_cast<PGNodeID>(
          reinterpret_cast<uintptr_t>(nodes[i].AsPointer()));
      if (nid == graph.inputNode() || nid == graph.outputNode())
        continue; // can't delete fixed nodes
      graph.removeNode(nid);
      markChanged();
    }
  }
}

void PostGraphEditorPanel::onUnlinkSelection(PostGraph &graph) {
  ed::SetCurrentEditor(m_ctx);

  std::vector<ed::NodeId> nodes;
  nodes.resize(ed::GetSelectedObjectCount());
  const int nodeCount = ed::GetSelectedNodes(nodes.data(), nodes.size());

  std::vector<PGLinkID> toRemove;
  toRemove.reserve(graph.links().size());

  for (int i = 0; i < nodeCount; ++i) {
    PGNodeID nid = static_cast<PGNodeID>(
        reinterpret_cast<uintptr_t>(nodes[i].AsPointer()));
    PGNode *n = graph.findNode(nid);
    if (!n)
      continue;

    for (const auto &l : graph.links()) {
      const bool touches =
          (n->inPin != 0 && (l.toPin == n->inPin || l.fromPin == n->inPin)) ||
          (n->outPin != 0 && (l.toPin == n->outPin || l.fromPin == n->outPin));
      if (touches)
        toRemove.push_back(l.id);
    }
  }

  if (toRemove.empty())
    return;

  // Deduplicate in case multiple nodes share the same link list.
  std::sort(toRemove.begin(), toRemove.end());
  toRemove.erase(std::unique(toRemove.begin(), toRemove.end()),
                 toRemove.end());

  for (PGLinkID lid : toRemove) {
    graph.removeLink(lid);
    markChanged();
  }
}

void PostGraphEditorPanel::unlinkNode(PostGraph &graph, PGNodeID nodeId) {
  PGNode *n = graph.findNode(nodeId);
  if (!n)
    return;

  std::vector<PGLinkID> toRemove;
  toRemove.reserve(graph.links().size());
  for (const auto &l : graph.links()) {
    const bool touches =
        (n->inPin != 0 && (l.toPin == n->inPin || l.fromPin == n->inPin)) ||
        (n->outPin != 0 && (l.toPin == n->outPin || l.fromPin == n->outPin));
    if (touches)
      toRemove.push_back(l.id);
  }

  if (toRemove.empty())
    return;

  std::sort(toRemove.begin(), toRemove.end());
  toRemove.erase(std::unique(toRemove.begin(), toRemove.end()),
                 toRemove.end());
  for (PGLinkID lid : toRemove)
    graph.removeLink(lid);
}

void PostGraphEditorPanel::tryInsertNodeIntoLink(PostGraph &graph,
                                                 PGNodeID nodeId,
                                                 PGLinkID linkId) {
  PGNode *n = graph.findNode(nodeId);
  if (!n || n->inPin == 0 || n->outPin == 0)
    return;

  const PGLink *found = nullptr;
  for (const auto &l : graph.links()) {
    if (l.id == linkId) {
      found = &l;
      break;
    }
  }
  if (!found)
    return;

  const PGLink old = *found;
  graph.removeLink(old.id);

  PGCompileError err{};
  const bool okA = graph.tryAddLink(old.fromPin, n->inPin, &err);
  if (!okA) {
    graph.tryAddLink(old.fromPin, old.toPin, &err);
    return;
  }

  const bool okB = graph.tryAddLink(n->outPin, old.toPin, &err);
  if (!okB) {
    // Remove the first inserted link.
    for (const auto &l : graph.links()) {
      if (l.fromPin == old.fromPin && l.toPin == n->inPin) {
        graph.removeLink(l.id);
        break;
      }
    }
    graph.tryAddLink(old.fromPin, old.toPin, &err);
    return;
  }

  markChanged();
}

} // namespace Nyx
