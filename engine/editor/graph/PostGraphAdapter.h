#pragma once

#include "editor/graph/GraphEditorInfra.h"
#include "post/FilterRegistry.h"
#include "post/PostGraph.h"

namespace Nyx {

class PostGraphAdapter final : public GraphEditorInfra::IGraphAdapter {
public:
  PostGraphAdapter(PostGraph &graph, const FilterRegistry &registry);

  const std::vector<GraphEditorInfra::PaletteItem> &paletteItems() const override;
  const std::vector<const char *> &paletteCategories() const override;
  bool addPaletteItem(uint32_t itemId, const ImVec2 &popupScreenPos) override;

private:
  PostGraph &m_graph;
  const FilterRegistry &m_registry;
  std::vector<GraphEditorInfra::PaletteItem> m_palette{};
  std::vector<const char *> m_categories{};
};

} // namespace Nyx
