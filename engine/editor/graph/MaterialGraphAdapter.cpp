#include "editor/graph/MaterialGraphAdapter.h"

#include "editor/graph/MaterialGraphSchema.h"
#include "render/material/MaterialSystem.h"

#include <imgui_node_editor.h>

namespace ed = ax::NodeEditor;

namespace Nyx {

namespace {

static uint32_t packSwizzle(uint8_t x, uint8_t y, uint8_t z, uint8_t w) {
  return uint32_t(x) | (uint32_t(y) << 8) | (uint32_t(z) << 16) |
         (uint32_t(w) << 24);
}

const std::vector<GraphEditorInfra::PaletteItem> &materialPaletteItems() {
  static const std::vector<GraphEditorInfra::PaletteItem> kItems = [] {
    std::vector<GraphEditorInfra::PaletteItem> out;
    const std::vector<MaterialNodeDesc> &nodes = materialNodePalette();
    out.reserve(nodes.size());
    for (const MaterialNodeDesc &d : nodes)
      out.push_back({static_cast<uint32_t>(d.type), d.name, d.category});
    return out;
  }();
  return kItems;
}

const std::vector<const char *> &materialPaletteCategories() {
  static const std::vector<const char *> kCategories = {"Input", "Constants",
                                                        "Textures", "Math",
                                                        "Output"};
  return kCategories;
}

} // namespace

MaterialGraphAdapter::MaterialGraphAdapter(
    MaterialGraph &graph, MaterialSystem &materials, MaterialHandle material,
    ax::NodeEditor::EditorContext *ctx, std::unordered_set<uint32_t> &posInitialized)
    : m_graph(graph), m_materials(materials), m_material(material), m_ctx(ctx),
      m_posInitialized(posInitialized) {}

const std::vector<GraphEditorInfra::PaletteItem> &
MaterialGraphAdapter::paletteItems() const {
  return materialPaletteItems();
}

const std::vector<const char *> &MaterialGraphAdapter::paletteCategories() const {
  return materialPaletteCategories();
}

bool MaterialGraphAdapter::addPaletteItem(uint32_t itemId,
                                          const ImVec2 &popupScreenPos) {
  MatNode n{};
  n.id = m_graph.nextNodeId++;
  n.type = static_cast<MatNodeType>(itemId);
  n.label = "";
  n.pos = {0.0f, 0.0f};
  n.posSet = false;

  switch (n.type) {
  case MatNodeType::ConstFloat:
    n.f.x = 0.0f;
    break;
  case MatNodeType::ConstVec3:
  case MatNodeType::ConstColor:
  case MatNodeType::ConstVec4:
    n.f = glm::vec4(1, 1, 1, 1);
    break;
  case MatNodeType::Texture2D:
    n.u.x = kInvalidTexIndex;
    n.u.y = 1;
    break;
  case MatNodeType::TextureMRA:
  case MatNodeType::NormalMap:
    n.u.x = kInvalidTexIndex;
    break;
  case MatNodeType::Swizzle:
    n.u.x = packSwizzle(0, 1, 2, 3);
    break;
  case MatNodeType::Channel:
    n.u.x = 0;
    break;
  default:
    break;
  }

  // Keep label stable from palette descriptor.
  if (const MaterialNodeDesc *d = findMaterialNodeDesc(n.type))
    n.label = d->name;

  m_graph.nodes.push_back(n);

  if (m_ctx) {
    ed::SetCurrentEditor(m_ctx);
    const ImVec2 canvasPos = ed::ScreenToCanvas(popupScreenPos);
    ed::SetNodePosition((ed::NodeId)(uintptr_t)n.id, canvasPos);
    ed::SetCurrentEditor(nullptr);
    for (auto &node : m_graph.nodes) {
      if (node.id == n.id) {
        node.pos = {canvasPos.x, canvasPos.y};
        node.posSet = true;
        break;
      }
    }
  }
  m_posInitialized.insert(n.id);

  m_materials.markGraphDirty(m_material);
  m_materials.syncMaterialFromGraph(m_material);
  return true;
}

} // namespace Nyx
