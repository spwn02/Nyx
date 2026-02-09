#include "MaterialGraphPanel.h"

#include "app/EngineContext.h"
#include "editor/graph/MaterialGraphSchema.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"
#include "render/material/GpuMaterial.h"
#include "render/material/MaterialSystem.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace ed = ax::NodeEditor;

namespace Nyx {

namespace {

static uint64_t pinId(MatNodeID node, uint32_t slot, bool isInput) {
  return (uint64_t(node) << 32) | (uint64_t(slot) << 1) | (isInput ? 1u : 0u);
}

static MatPin decodePin(uint64_t id, bool &isInput) {
  isInput = (id & 1u) != 0u;
  MatPin p{};
  p.node = (MatNodeID)(id >> 32);
  p.slot = (uint32_t)((id >> 1) & 0x7FFFFFFFu);
  return p;
}

static uint64_t linkId(const MatLink &l) { return l.id; }

} // namespace

void MaterialGraphPanel::drawGraph(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);

  if (m_requestAutoLayout) {
    m_requestAutoLayout = false;
    float xInput = 0.0f;
    float xConst = 200.0f;
    float xTex = 400.0f;
    float xMath = 600.0f;
    float xOut = 800.0f;

    float yInput = 0.0f;
    float yConst = 0.0f;
    float yTex = 0.0f;
    float yMath = 0.0f;
    float yOut = 0.0f;

    auto place = [&](MatNode &n, float x, float &y) {
      n.pos = {x, y};
      n.posSet = true;
      y += 120.0f;
    };

    for (auto &n : g.nodes) {
      switch (n.type) {
      case MatNodeType::UV0:
      case MatNodeType::NormalWS:
      case MatNodeType::ViewDirWS:
        place(n, xInput, yInput);
        break;
      case MatNodeType::ConstFloat:
      case MatNodeType::ConstVec3:
      case MatNodeType::ConstColor:
      case MatNodeType::ConstVec4:
        place(n, xConst, yConst);
        break;
      case MatNodeType::Texture2D:
      case MatNodeType::TextureMRA:
      case MatNodeType::NormalMap:
        place(n, xTex, yTex);
        break;
      case MatNodeType::SurfaceOutput:
        place(n, xOut, yOut);
        break;
      default:
        place(n, xMath, yMath);
        break;
      }
    }
    m_posInitialized.clear();
  }

  if (g.nextNodeId == 0)
    g.nextNodeId = 1;
  for (const auto &n : g.nodes)
    g.nextNodeId = std::max(g.nextNodeId, n.id + 1);
  if (g.nextLinkId == 0)
    g.nextLinkId = 1;
  for (const auto &l : g.links)
    g.nextLinkId = std::max(g.nextLinkId, l.id + 1);
  for (auto &l : g.links) {
    if (l.id == 0)
      l.id = g.nextLinkId++;
  }

  ed::SetCurrentEditor(m_ctx);
  ed::Begin("MaterialGraphCanvas");
  const ImGuiIO &io = ImGui::GetIO();
  const bool hovered =
      ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (hovered ||
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    engine.requestUiBlockGlobalShortcuts();
  }
  for (auto &n : g.nodes) {
    if (n.posSet && m_posInitialized.insert(n.id).second) {
      ed::SetNodePosition(ed::NodeId((uintptr_t)n.id),
                          ImVec2(n.pos.x, n.pos.y));
    }
    ed::BeginNode(ed::NodeId((uintptr_t)n.id));
    ImGui::TextUnformatted(n.label.empty() ? materialNodeName(n.type)
                                           : n.label.c_str());
    ImGui::Separator();

    const uint32_t inCount = materialInputCount(n.type);
    const uint32_t outCount = materialOutputCount(n.type);

    for (uint32_t i = 0; i < inCount; ++i) {
      ed::BeginPin(ed::PinId((uintptr_t)pinId(n.id, i, true)),
                   ed::PinKind::Input);
      ImGui::TextUnformatted(materialInputName(n.type, i));
      ed::EndPin();
    }

    for (uint32_t i = 0; i < outCount; ++i) {
      ed::BeginPin(ed::PinId((uintptr_t)pinId(n.id, i, false)),
                   ed::PinKind::Output);
      ImGui::TextUnformatted(materialOutputName(n.type, i));
      ed::EndPin();
    }

    if (n.type == MatNodeType::Texture2D || n.type == MatNodeType::TextureMRA ||
        n.type == MatNodeType::NormalMap) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::PushID((int)n.id);

      uint32_t glTex = 0;
      if (n.u.x != kInvalidTexIndex) {
        glTex = materials.textures().glTexByIndex(n.u.x);
      }
      const ImVec2 thumb(48.0f, 48.0f);
      if (glTex != 0) {
        ImGui::Image((ImTextureID)(uintptr_t)glTex, thumb);
      } else {
        ImGui::Button("##tex", thumb);
      }
      ImGui::SameLine();
      ImGui::BeginGroup();

      if (ImGui::SmallButton("Load")) {
        const char *filters = "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr,cube";
        if (auto path = FileDialogs::openFile("Open Texture", filters, nullptr)) {
          if (!path->empty()) {
            const bool wantSRGB =
                (n.type == MatNodeType::Texture2D) ? (n.u.y != 0) : false;
            uint32_t idx = materials.textures().getOrCreate2D(*path, wantSRGB);
            if (idx != TextureTable::Invalid) {
              n.u.x = idx;
              n.path = *path;
              materials.markGraphDirty(m_mat);
              materials.syncMaterialFromGraph(m_mat);
            }
          }
        }
      }

      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload *pl =
                ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
          const char *path = (const char *)pl->Data;
          if (path && path[0]) {
            const bool wantSRGB =
                (n.type == MatNodeType::Texture2D) ? (n.u.y != 0) : false;
            uint32_t idx = materials.textures().getOrCreate2D(path, wantSRGB);
            if (idx != TextureTable::Invalid) {
              n.u.x = idx;
              n.path = path;
              materials.markGraphDirty(m_mat);
              materials.syncMaterialFromGraph(m_mat);
            }
          }
        }
        ImGui::EndDragDropTarget();
      }

      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) {
        n.u.x = kInvalidTexIndex;
        n.path.clear();
        materials.markGraphDirty(m_mat);
        materials.syncMaterialFromGraph(m_mat);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Reload")) {
        if (!n.path.empty()) {
          const bool wantSRGB =
              (n.type == MatNodeType::Texture2D) ? (n.u.y != 0) : false;
          uint32_t idx = materials.textures().getOrCreate2D(n.path, wantSRGB);
          if (idx != TextureTable::Invalid) {
            materials.textures().reloadByIndex(idx);
            n.u.x = idx;
            materials.markGraphDirty(m_mat);
            materials.syncMaterialFromGraph(m_mat);
          }
        }
      }

      ImGui::EndGroup();
      ImGui::PopID();
    }

    ed::EndNode();

    const ImVec2 pos = ed::GetNodePosition(ed::NodeId((uintptr_t)n.id));
    n.pos = {pos.x, pos.y};
    n.posSet = true;
  }
  for (auto &L : g.links) {
    ed::Link(ed::LinkId((uintptr_t)linkId(L)),
             ed::PinId((uintptr_t)pinId(L.from.node, L.from.slot, false)),
             ed::PinId((uintptr_t)pinId(L.to.node, L.to.slot, true)));
  }
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      if (a && b) {
        bool aIn = false;
        bool bIn = false;
        MatPin pa =
            decodePin((uint64_t)reinterpret_cast<uintptr_t>(a.AsPointer()), aIn);
        MatPin pb =
            decodePin((uint64_t)reinterpret_cast<uintptr_t>(b.AsPointer()), bIn);

        if (aIn == bIn) {
          ed::RejectNewItem();
        } else {
          MatPin from = pa;
          MatPin to = pb;
          if (aIn) {
            from = pb;
            to = pa;
          }

          if (ed::AcceptNewItem()) {
            g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                         [&](const MatLink &l) {
                                           return l.to.node == to.node &&
                                                  l.to.slot == to.slot;
                                         }),
                          g.links.end());
            MatLink L{};
            L.id = g.nextLinkId++;
            L.from = from;
            L.to = to;
            g.links.push_back(L);
            materials.markGraphDirty(m_mat);
            materials.syncMaterialFromGraph(m_mat);
          }
        }
      }
    }
  }
  ed::EndCreate();
  if (ed::BeginDelete()) {
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid)) {
      if (ed::AcceptDeletedItem()) {
        const uint64_t id =
            (uint64_t)reinterpret_cast<uintptr_t>(lid.AsPointer());
        g.links.erase(
            std::remove_if(g.links.begin(), g.links.end(), [&](const MatLink &l) {
              return linkId(l) == id;
            }),
            g.links.end());
        materials.markGraphDirty(m_mat);
        materials.syncMaterialFromGraph(m_mat);
      }
    }

    ed::NodeId nid;
    while (ed::QueryDeletedNode(&nid)) {
      if (ed::AcceptDeletedItem()) {
        const MatNodeID id = (MatNodeID)reinterpret_cast<uintptr_t>(nid.AsPointer());
        g.links.erase(
            std::remove_if(g.links.begin(), g.links.end(),
                           [&](const MatLink &l) {
                             return l.from.node == id || l.to.node == id;
                           }),
            g.links.end());
        g.nodes.erase(
            std::remove_if(g.nodes.begin(), g.nodes.end(),
                           [&](const MatNode &n) { return n.id == id; }),
            g.nodes.end());
        if (m_selectedNode == id)
          m_selectedNode = 0;
        materials.markGraphDirty(m_mat);
        materials.syncMaterialFromGraph(m_mat);
      }
    }
  }
  ed::EndDelete();
  {
    ed::NodeId hoveredNode = ed::GetHoveredNode();
    if (hoveredNode &&
        (ImGui::IsMouseClicked(0) || ImGui::IsMouseReleased(0) ||
         ImGui::IsMouseDown(0))) {
      m_selectedNode =
          (uint32_t)reinterpret_cast<uintptr_t>(hoveredNode.AsPointer());
    }
    std::array<ed::NodeId, 4> nodes{};
    const int nodeCount = ed::GetSelectedNodes(nodes.data(), (int)nodes.size());
    if (nodeCount > 0) {
      m_selectedNode = (uint32_t)reinterpret_cast<uintptr_t>(nodes[0].AsPointer());
    } else if (ed::IsBackgroundClicked()) {
      m_selectedNode = 0;
    }
  }

  ed::End();
  ed::SetCurrentEditor(nullptr);
  const bool graphFocused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

  if (graphFocused && m_selectedNode != 0) {
    if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D)) {
      MatNode *src = nullptr;
      for (auto &n : g.nodes) {
        if (n.id == m_selectedNode) {
          src = &n;
          break;
        }
      }
      if (src) {
        MatNode dup = *src;
        dup.id = g.nextNodeId++;
        dup.pos = src->pos + glm::vec2(30.0f, 30.0f);
        dup.posSet = true;
        g.nodes.push_back(dup);
        m_selectedNode = dup.id;
        m_posInitialized.erase(dup.id);
        materials.markGraphDirty(m_mat);
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
        ImGui::IsKeyPressed(ImGuiKey_X)) {
      const MatNodeID id = m_selectedNode;
      g.links.erase(
          std::remove_if(g.links.begin(), g.links.end(),
                         [&](const MatLink &l) {
                           return l.from.node == id || l.to.node == id;
                         }),
          g.links.end());
      g.nodes.erase(
          std::remove_if(g.nodes.begin(), g.nodes.end(),
                         [&](const MatNode &n) { return n.id == id; }),
          g.nodes.end());
      m_selectedNode = 0;
      materials.markGraphDirty(m_mat);
      materials.syncMaterialFromGraph(m_mat);
    }
  }

  if (m_requestNavigateToContent) {
    m_requestNavigateToContent = false;
    ed::SetCurrentEditor(m_ctx);
    ed::NavigateToContent(0.0f);
    ed::SetCurrentEditor(nullptr);
  }
}

} // namespace Nyx
