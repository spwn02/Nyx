#include "editor/graph/PostGraphAdapter.h"

#include <algorithm>
#include <cstring>

namespace Nyx {

PostGraphAdapter::PostGraphAdapter(PostGraph &graph,
                                   const FilterRegistry &registry)
    : m_graph(graph), m_registry(registry) {
  const auto &all = m_registry.types();
  m_palette.reserve(all.size());
  for (const auto &t : all) {
    m_palette.push_back({static_cast<uint32_t>(t.id), t.name, t.category});
  }

  m_categories.reserve(m_palette.size());
  for (const auto &it : m_palette) {
    if (std::find_if(m_categories.begin(), m_categories.end(),
                     [&](const char *cat) {
                       return std::strcmp(cat, it.category) == 0;
                     }) == m_categories.end()) {
      m_categories.push_back(it.category);
    }
  }
}

const std::vector<GraphEditorInfra::PaletteItem> &
PostGraphAdapter::paletteItems() const {
  return m_palette;
}

const std::vector<const char *> &PostGraphAdapter::paletteCategories() const {
  return m_categories;
}

bool PostGraphAdapter::addPaletteItem(uint32_t itemId,
                                      const ImVec2 &popupScreenPos) {
  (void)popupScreenPos;
  const FilterTypeInfo *t = m_registry.find(static_cast<FilterTypeId>(itemId));
  if (!t)
    return false;
  std::vector<float> defaults;
  defaults.reserve(t->paramCount);
  for (uint32_t i = 0; i < t->paramCount; ++i)
    defaults.push_back(t->params[i].defaultValue);
  const char *label =
      (t->defaultLabel && t->defaultLabel[0]) ? t->defaultLabel : t->name;
  m_graph.addFilter(static_cast<uint32_t>(t->id), label, defaults);
  return true;
}

} // namespace Nyx
