#include "MaterialGraphPanel.h"

#include "app/EngineContext.h"
#include "editor/ui/UiPayloads.h"
#include "platform/FileDialogs.h"
#include "render/material/GpuMaterial.h"
#include "render/material/MaterialGraph.h"
#include "render/material/MaterialSystem.h"

#include <imgui.h>
#include <imgui_node_editor.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Nyx {

namespace {

struct NodeDesc final {
  MatNodeType type;
  const char *name;
  const char *category;
};

static const NodeDesc kNodes[] = {
    {MatNodeType::UV0, "UV0", "Input"},
    {MatNodeType::NormalWS, "NormalWS", "Input"},

    {MatNodeType::ConstFloat, "Float", "Constants"},
    {MatNodeType::ConstVec3, "Vec3", "Constants"},
    {MatNodeType::ConstColor, "Color", "Constants"},
    {MatNodeType::ConstVec4, "Vec4", "Constants"},

    {MatNodeType::Texture2D, "Texture2D", "Textures"},
    {MatNodeType::TextureMRA, "Texture MRA", "Textures"},
    {MatNodeType::NormalMap, "Normal Map", "Textures"},

    {MatNodeType::Add, "Add", "Math"},
    {MatNodeType::Sub, "Sub", "Math"},
    {MatNodeType::Mul, "Mul", "Math"},
    {MatNodeType::Div, "Div", "Math"},
    {MatNodeType::Clamp01, "Clamp01", "Math"},
    {MatNodeType::OneMinus, "OneMinus", "Math"},
    {MatNodeType::Lerp, "Lerp", "Math"},

    {MatNodeType::SurfaceOutput, "Surface Output", "Output"},
};

static const char *nodeName(MatNodeType t) {
  for (const auto &n : kNodes) {
    if (n.type == t)
      return n.name;
  }
  return "Node";
}

static const char *nodeCategory(MatNodeType t) {
  for (const auto &n : kNodes) {
    if (n.type == t)
      return n.category;
  }
  return "Other";
}

static uint32_t inputCount(MatNodeType t) {
  switch (t) {
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
    return 1; // UV
  case MatNodeType::NormalMap:
    return 3; // UV, (unused), Strength
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
    return 2;
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
    return 1;
  case MatNodeType::Lerp:
    return 3;
  case MatNodeType::SurfaceOutput:
    return 7;
  default:
    return 0;
  }
}

static uint32_t outputCount(MatNodeType t) {
  switch (t) {
  case MatNodeType::SurfaceOutput:
    return 0;
  case MatNodeType::ConstFloat:
  case MatNodeType::ConstVec3:
  case MatNodeType::ConstColor:
  case MatNodeType::ConstVec4:
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
  case MatNodeType::NormalMap:
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
  case MatNodeType::Lerp:
  case MatNodeType::UV0:
  case MatNodeType::NormalWS:
    return 1;
  default:
    return 1;
  }
}

static const char *inputName(MatNodeType t, uint32_t slot) {
  switch (t) {
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
    return (slot == 0) ? "UV" : "";
  case MatNodeType::NormalMap:
    if (slot == 0)
      return "UV";
    if (slot == 1)
      return "NormalWS";
    if (slot == 2)
      return "Strength";
    return "";
  case MatNodeType::Add:
  case MatNodeType::Sub:
  case MatNodeType::Mul:
  case MatNodeType::Div:
    return (slot == 0) ? "A" : "B";
  case MatNodeType::Clamp01:
  case MatNodeType::OneMinus:
    return "In";
  case MatNodeType::Lerp:
    return slot == 0 ? "A" : (slot == 1 ? "B" : "T");
  case MatNodeType::SurfaceOutput:
    switch (slot) {
    case 0:
      return "BaseColor";
    case 1:
      return "Metallic";
    case 2:
      return "Roughness";
    case 3:
      return "NormalWS";
    case 4:
      return "AO";
    case 5:
      return "Emissive";
    case 6:
      return "Alpha";
    default:
      return "";
    }
  default:
    return "";
  }
}

static const char *outputName(MatNodeType t, uint32_t slot) {
  (void)slot;
  switch (t) {
  case MatNodeType::ConstFloat:
    return "F";
  case MatNodeType::ConstVec3:
  case MatNodeType::ConstColor:
    return "RGB";
  case MatNodeType::ConstVec4:
  case MatNodeType::Texture2D:
    return "RGBA";
  case MatNodeType::TextureMRA:
    return "MRA";
  case MatNodeType::Swizzle:
    return "Out";
  case MatNodeType::Channel:
    return "Ch";
  case MatNodeType::Split:
    if (slot == 0)
      return "X";
    if (slot == 1)
      return "Y";
    if (slot == 2)
      return "Z";
    if (slot == 3)
      return "W";
    return "Out";
  case MatNodeType::NormalMap:
    return "Normal";
  case MatNodeType::UV0:
    return "UV";
  case MatNodeType::NormalWS:
    return "Normal";
  case MatNodeType::ViewDirWS:
    return "ViewDir";
  default:
    return "Out";
  }
}

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

static uint32_t packSwizzle(uint8_t x, uint8_t y, uint8_t z, uint8_t w) {
  return uint32_t(x) | (uint32_t(y) << 8) | (uint32_t(z) << 16) |
         (uint32_t(w) << 24);
}

static void unpackSwizzle(uint32_t v, uint8_t &x, uint8_t &y, uint8_t &z,
                          uint8_t &w) {
  x = (uint8_t)(v & 0xFF);
  y = (uint8_t)((v >> 8) & 0xFF);
  z = (uint8_t)((v >> 16) & 0xFF);
  w = (uint8_t)((v >> 24) & 0xFF);
}

} // namespace

void MaterialGraphPanel::ensureContext() {
  if (m_ctx)
    return;
  ed::Config cfg{};
  cfg.SettingsFile = "nyx_matgraph.json";
  m_ctx = ed::CreateEditor(&cfg);
}

void MaterialGraphPanel::setMaterial(MaterialHandle h) {
  if (m_mat.slot == h.slot && m_mat.gen == h.gen)
    return;
  m_mat = h;
  m_selectedNode = 0;
  m_selectedLink = 0;
  m_posInitialized.clear();
}

void MaterialGraphPanel::ensureDefaultGraph(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);
  if (!g.nodes.empty())
    return;

  materials.ensureGraphFromMaterial(m_mat, true);
  m_posInitialized.clear();
}

void MaterialGraphPanel::drawToolbar(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  if (ImGui::Button("Auto Layout")) {
    m_requestAutoLayout = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Zoom to Fit")) {
    m_requestNavigateToContent = true;
  }
  ImGui::SameLine();

  if (ImGui::Button("Compile & Upload")) {
    materials.markGraphDirty(m_mat);
    materials.uploadIfDirty();
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset Defaults")) {
    materials.ensureGraphFromMaterial(m_mat, true);
    m_posInitialized.clear();
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy Graph")) {
    m_clipboard = materials.graph(m_mat);
    m_hasClipboard = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Paste Graph") && m_hasClipboard) {
    MaterialGraph &g = materials.graph(m_mat);
    g.nodes.clear();
    g.links.clear();
    g.nextNodeId = 1;
    g.nextLinkId = 1;

    std::unordered_map<MatNodeID, MatNodeID> idMap;
    idMap.reserve(m_clipboard.nodes.size());
    for (const auto &src : m_clipboard.nodes) {
      MatNode n = src;
      n.id = g.nextNodeId++;
      idMap[src.id] = n.id;
      g.nodes.push_back(std::move(n));
    }
    for (const auto &src : m_clipboard.links) {
      MatLink l = src;
      l.id = g.nextLinkId++;
      l.from.node = idMap[l.from.node];
      l.to.node = idMap[l.to.node];
      g.links.push_back(std::move(l));
    }

    materials.markGraphDirty(m_mat);
    m_posInitialized.clear();
  }

  const std::string &err = materials.graphError(m_mat);
  if (!err.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Graph Error: %s",
                       err.c_str());
  }
}

void MaterialGraphPanel::drawNodeProps(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);

  ImGui::TextUnformatted("Properties");
  ImGui::Separator();

  if (m_selectedNode == 0) {
    ImGui::TextUnformatted("Select a node to edit properties.");
    return;
  }

  MatNode *n = nullptr;
  for (auto &node : g.nodes) {
    if (node.id == m_selectedNode) {
      n = &node;
      break;
    }
  }
  if (!n) {
    ImGui::TextUnformatted("Invalid selection.");
    return;
  }

  ImGui::Text("Node: %s", nodeName(n->type));
  ImGui::Separator();

  bool changed = false;

  switch (n->type) {
  case MatNodeType::ConstFloat: {
    changed |= ImGui::DragFloat("Value", &n->f.x, 0.01f, -10.0f, 10.0f);
  } break;
  case MatNodeType::ConstVec3: {
    changed |= ImGui::ColorEdit3("Value", &n->f.x);
  } break;
  case MatNodeType::ConstColor: {
    changed |= ImGui::ColorEdit3("Color", &n->f.x);
  } break;
  case MatNodeType::ConstVec4: {
    changed |= ImGui::ColorEdit4("Value", &n->f.x);
  } break;
  case MatNodeType::Texture2D:
  case MatNodeType::TextureMRA:
  case MatNodeType::NormalMap: {
    const bool isSRGB = (n->type == MatNodeType::Texture2D) && (n->u.y != 0);
    if (n->type == MatNodeType::Texture2D) {
      bool srgb = n->u.y != 0;
      if (ImGui::Checkbox("sRGB", &srgb)) {
        n->u.y = srgb ? 1u : 0u;
        if (!n->path.empty()) {
          uint32_t idx =
              materials.textures().getOrCreate2D(n->path, srgb);
          if (idx != TextureTable::Invalid)
            n->u.x = idx;
        }
        changed = true;
      }
    }

    if (ImGui::Button("Open...")) {
      const char *filters = "png,jpg,jpeg,tga,bmp,ktx,ktx2,hdr,exr,cube";
      if (auto path =
              FileDialogs::openFile("Open Texture", filters, nullptr)) {
        if (!path->empty()) {
          const bool wantSRGB =
              (n->type == MatNodeType::Texture2D) ? isSRGB : false;
          uint32_t idx = materials.textures().getOrCreate2D(*path, wantSRGB);
          if (idx != TextureTable::Invalid) {
            n->u.x = idx;
            n->path = *path;
            changed = true;
          }
        }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
      n->u.x = kInvalidTexIndex;
      n->path.clear();
      changed = true;
    }

    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *pl =
              ImGui::AcceptDragDropPayload(UiPayload::TexturePath)) {
        const char *path = (const char *)pl->Data;
        if (path && path[0]) {
          const bool wantSRGB =
              (n->type == MatNodeType::Texture2D) ? isSRGB : false;
          uint32_t idx = materials.textures().getOrCreate2D(path, wantSRGB);
          if (idx != TextureTable::Invalid) {
            n->u.x = idx;
            n->path = path;
            changed = true;
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
  } break;
  case MatNodeType::Swizzle: {
    uint8_t sx, sy, sz, sw;
    unpackSwizzle(n->u.x, sx, sy, sz, sw);
    const char *opts[] = {"X", "Y", "Z", "W"};
    int ix = (sx < 4) ? (int)sx : 0;
    int iy = (sy < 4) ? (int)sy : 1;
    int iz = (sz < 4) ? (int)sz : 2;
    int iw = (sw < 4) ? (int)sw : 3;
    if (ImGui::Combo("X", &ix, opts, 4) ||
        ImGui::Combo("Y", &iy, opts, 4) ||
        ImGui::Combo("Z", &iz, opts, 4) ||
        ImGui::Combo("W", &iw, opts, 4)) {
      n->u.x = packSwizzle((uint8_t)ix, (uint8_t)iy, (uint8_t)iz,
                           (uint8_t)iw);
      changed = true;
    }
  } break;
  case MatNodeType::Channel: {
    const char *opts[] = {"R", "G", "B", "A"};
    int ch = (n->u.x < 4) ? (int)n->u.x : 0;
    if (ImGui::Combo("Channel", &ch, opts, 4)) {
      n->u.x = (uint32_t)ch;
      changed = true;
    }
    ImGui::TextDisabled("Use with MRA to extract channels.");
  } break;
  default:
    ImGui::TextUnformatted("No editable properties for this node.");
    break;
  }

  if (changed) {
    materials.markGraphDirty(m_mat);
    materials.syncMaterialFromGraph(m_mat);
  }

  // no child here; parent handles layout
}

void MaterialGraphPanel::drawAddMenu(EngineContext &engine) {
  if (m_requestOpenAddMenu) {
    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
    ImGui::OpenPopup("AddMaterialNode");
    m_requestOpenAddMenu = false;
  } else {
    ImGui::SetNextWindowPos(m_popupPos, ImGuiCond_Appearing);
  }

  if (!ImGui::BeginPopup("AddMaterialNode", ImGuiWindowFlags_AlwaysAutoResize))
    return;

  ImGui::TextUnformatted("Add Node");
  ImGui::Separator();

  ImGui::SetNextItemWidth(260.0f);
  ImGui::InputTextWithHint("##search", "Search nodes...", m_search,
                           sizeof(m_search));
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere(-1);

  ImGui::Separator();

  // Collect unique categories in order
  std::vector<const char *> cats;
  for (const auto &n : kNodes) {
    bool seen = false;
    for (auto *c : cats) {
      if (std::string(c) == std::string(n.category)) {
        seen = true;
        break;
      }
    }
    if (!seen)
      cats.push_back(n.category);
  }

  auto &materials = engine.materials();
  MaterialGraph &g = materials.graph(m_mat);

  for (const char *cat : cats) {
    bool anyInCat = false;
    for (const auto &n : kNodes) {
      if (std::string(n.category) != std::string(cat))
        continue;
      if (!passFilterCI(m_search, n.name))
        continue;
      anyInCat = true;
      break;
    }
    if (!anyInCat)
      continue;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
    if (ImGui::TreeNodeEx(cat, flags)) {
      for (const auto &nd : kNodes) {
        if (std::string(nd.category) != std::string(cat))
          continue;
        if (!passFilterCI(m_search, nd.name))
          continue;

      if (ImGui::Selectable(nd.name)) {
          MatNode n{};
          n.id = g.nextNodeId++;
          n.type = nd.type;
          n.label = nd.name;
          n.pos = {0.0f, 0.0f};
          n.posSet = false;

          if (n.type == MatNodeType::ConstFloat) {
            n.f.x = 0.0f;
          } else if (n.type == MatNodeType::ConstVec3 ||
                     n.type == MatNodeType::ConstColor) {
            n.f = glm::vec4(1, 1, 1, 1);
          } else if (n.type == MatNodeType::ConstVec4) {
            n.f = glm::vec4(1, 1, 1, 1);
          } else if (n.type == MatNodeType::Texture2D) {
            n.u.x = kInvalidTexIndex;
            n.u.y = 1; // sRGB default
          } else if (n.type == MatNodeType::TextureMRA ||
                     n.type == MatNodeType::NormalMap) {
            n.u.x = kInvalidTexIndex;
          } else if (n.type == MatNodeType::Swizzle) {
            n.u.x = packSwizzle(0, 1, 2, 3);
          } else if (n.type == MatNodeType::Channel) {
            n.u.x = 0;
          }

          g.nodes.push_back(n);

          ed::SetCurrentEditor(m_ctx);
          const ImVec2 canvasPos = ed::ScreenToCanvas(m_popupPos);
          ed::SetNodePosition((ed::NodeId)(uintptr_t)n.id, canvasPos);
          ed::SetCurrentEditor(nullptr);
          for (auto &node : g.nodes) {
            if (node.id == n.id) {
              node.pos = {canvasPos.x, canvasPos.y};
              node.posSet = true;
              break;
            }
          }
          m_posInitialized.insert(n.id);

          materials.markGraphDirty(m_mat);
          materials.syncMaterialFromGraph(m_mat);
          ImGui::CloseCurrentPopup();
          m_openAddMenu = false;
        }
      }
      ImGui::TreePop();
    }
  }

  ImGui::EndPopup();
}

void MaterialGraphPanel::drawGraph(EngineContext &engine) {
  auto &materials = engine.materials();
  if (!materials.isAlive(m_mat))
    return;

  MaterialGraph &g = materials.graph(m_mat);

  if (m_requestAutoLayout) {
    m_requestAutoLayout = false;
    // Simple layout: columns by category
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

  // Ensure links have stable ids
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

  // Open add menu on Shift+A (handled outside editor to avoid canvas offsets)
  const ImGuiIO &io = ImGui::GetIO();
  const bool hovered = ImGui::IsWindowHovered(
      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  if (hovered ||
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    engine.requestUiBlockGlobalShortcuts();
  }

  // Nodes
  for (auto &n : g.nodes) {
    if (n.posSet && m_posInitialized.insert(n.id).second) {
      ed::SetNodePosition(ed::NodeId((uintptr_t)n.id),
                          ImVec2(n.pos.x, n.pos.y));
    }
    ed::BeginNode(ed::NodeId((uintptr_t)n.id));
    ImGui::TextUnformatted(n.label.empty() ? nodeName(n.type) : n.label.c_str());
    ImGui::Separator();

    const uint32_t inCount = inputCount(n.type);
    const uint32_t outCount = outputCount(n.type);

    for (uint32_t i = 0; i < inCount; ++i) {
      ed::BeginPin(ed::PinId((uintptr_t)pinId(n.id, i, true)),
                   ed::PinKind::Input);
      ImGui::TextUnformatted(inputName(n.type, i));
      ed::EndPin();
    }

    for (uint32_t i = 0; i < outCount; ++i) {
      ed::BeginPin(ed::PinId((uintptr_t)pinId(n.id, i, false)),
                   ed::PinKind::Output);
      ImGui::TextUnformatted(outputName(n.type, i));
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
        if (auto path =
                FileDialogs::openFile("Open Texture", filters, nullptr)) {
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

  // Links
  for (auto &L : g.links) {
    ed::Link(ed::LinkId((uintptr_t)linkId(L)),
             ed::PinId((uintptr_t)pinId(L.from.node, L.from.slot, false)),
             ed::PinId((uintptr_t)pinId(L.to.node, L.to.slot, true)));
  }

  // Create links
  if (ed::BeginCreate()) {
    ed::PinId a, b;
    if (ed::QueryNewLink(&a, &b)) {
      if (a && b) {
        bool aIn = false;
        bool bIn = false;
        MatPin pa =
            decodePin((uint64_t)reinterpret_cast<uintptr_t>(a.AsPointer()),
                      aIn);
        MatPin pb =
            decodePin((uint64_t)reinterpret_cast<uintptr_t>(b.AsPointer()),
                      bIn);

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
            g.links.erase(
                std::remove_if(g.links.begin(), g.links.end(),
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

  // Delete links/nodes
  if (ed::BeginDelete()) {
    ed::LinkId lid;
    while (ed::QueryDeletedLink(&lid)) {
      if (ed::AcceptDeletedItem()) {
        const uint64_t id =
            (uint64_t)reinterpret_cast<uintptr_t>(lid.AsPointer());
        g.links.erase(std::remove_if(g.links.begin(), g.links.end(),
                                     [&](const MatLink &l) {
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
        const MatNodeID id =
            (MatNodeID)reinterpret_cast<uintptr_t>(nid.AsPointer());
        g.links.erase(
            std::remove_if(g.links.begin(), g.links.end(),
                           [&](const MatLink &l) {
                             return l.from.node == id || l.to.node == id;
                           }),
            g.links.end());
        g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                     [&](const MatNode &n) {
                                       return n.id == id;
                                     }),
                      g.nodes.end());
        if (m_selectedNode == id)
          m_selectedNode = 0;
        materials.markGraphDirty(m_mat);
        materials.syncMaterialFromGraph(m_mat);
      }
    }
  }
  ed::EndDelete();

  // Selection
  {
    ed::NodeId hovered = ed::GetHoveredNode();
    if (hovered &&
        (ImGui::IsMouseClicked(0) || ImGui::IsMouseReleased(0) ||
         ImGui::IsMouseDown(0))) {
      m_selectedNode =
          (uint32_t)reinterpret_cast<uintptr_t>(hovered.AsPointer());
    }
    std::array<ed::NodeId, 4> nodes{};
    const int nodeCount = ed::GetSelectedNodes(nodes.data(), (int)nodes.size());
    if (nodeCount > 0) {
      m_selectedNode =
          (uint32_t)reinterpret_cast<uintptr_t>(nodes[0].AsPointer());
    } else if (ed::IsBackgroundClicked()) {
      m_selectedNode = 0;
    }
  }

  ed::End();
  ed::SetCurrentEditor(nullptr);

  // const ImGuiIO &io = ImGui::GetIO();
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
      g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
                                   [&](const MatNode &n) {
                                     return n.id == id;
                                   }),
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

void MaterialGraphPanel::draw(EngineContext &engine) {
  ensureContext();

  ImGui::SetNextWindowSize(ImVec2(1200, 720), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Material Graph")) {
    ImGui::End();
    return;
  }

  m_hovered = ImGui::IsWindowHovered(
      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
      ImGuiHoveredFlags_ChildWindows);
  if (m_hovered ||
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    engine.requestUiBlockGlobalShortcuts();
  }

  if (!engine.materials().isAlive(m_mat)) {
    ImGui::TextUnformatted("No material selected.");
    ImGui::End();
    return;
  }

  ensureDefaultGraph(engine);
  drawToolbar(engine);

  ImGui::Separator();

  const float rightWidth = 320.0f;
  if (ImGui::BeginTable("MatGraphLayout", 2,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Props", ImGuiTableColumnFlags_WidthFixed, rightWidth);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::BeginChild("MatGraphLeft", ImVec2(0, 0), false);
    drawGraph(engine);
    ImGui::EndChild();

    ImGui::TableNextColumn();
    ImGui::BeginChild("MatGraphRight", ImVec2(0, 0), true);
    drawNodeProps(engine);
    ImGui::EndChild();
    ImGui::EndTable();
  }

  const ImGuiIO &io = ImGui::GetIO();
  const bool windowHovered = ImGui::IsWindowHovered(
      ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
      ImGuiHoveredFlags_ChildWindows);
  if (windowHovered && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_A)) {
    m_openAddMenu = true;
    m_requestOpenAddMenu = true;
    m_search[0] = 0;
    m_popupPos = ImGui::GetMousePos();
  }

  drawAddMenu(engine);

  ImGui::End();
}

} // namespace Nyx
