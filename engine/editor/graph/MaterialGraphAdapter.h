#pragma once

#include "editor/graph/GraphEditorInfra.h"
#include "material/MaterialHandle.h"
#include "render/material/MaterialGraph.h"

#include <imgui.h>
#include <unordered_set>

namespace ax::NodeEditor {
struct EditorContext;
} // namespace ax::NodeEditor

namespace Nyx {

class MaterialSystem;

class MaterialGraphAdapter final : public GraphEditorInfra::IGraphAdapter {
public:
  MaterialGraphAdapter(MaterialGraph &graph, MaterialSystem &materials,
                       MaterialHandle material, ax::NodeEditor::EditorContext *ctx,
                       std::unordered_set<uint32_t> &posInitialized);

  const std::vector<GraphEditorInfra::PaletteItem> &paletteItems() const override;
  const std::vector<const char *> &paletteCategories() const override;
  bool addPaletteItem(uint32_t itemId, const ImVec2 &popupScreenPos) override;

private:
  MaterialGraph &m_graph;
  MaterialSystem &m_materials;
  MaterialHandle m_material;
  ax::NodeEditor::EditorContext *m_ctx = nullptr;
  std::unordered_set<uint32_t> &m_posInitialized;
};

} // namespace Nyx
