#include "PostGraphEditorPanel.h"

#include "editor/graph/GraphEditorInfra.h"
#include "editor/graph/PostGraphAdapter.h"

#include <imgui_node_editor.h>

#include <cstdint>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Nyx {

void PostGraphEditorPanel::drawAddMenu(PostGraph &graph,
                                       const FilterRegistry &registry) {
  GraphEditorInfra::PopupState popup{
      m_openAddMenu, m_requestOpenAddMenu, m_popupPos};
  PostGraphAdapter adapter(graph, registry);
  if (GraphEditorInfra::drawPalettePopup(
          "AddFilterNode", "Add Filter", "Search filters...", popup, m_search,
          sizeof(m_search), adapter)) {
    markChanged();
  }
  m_openAddMenu = popup.open;
  m_requestOpenAddMenu = popup.requestOpen;
  m_popupPos = popup.popupPos;
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
      if (PGNode *n = graph.findNode(id))
        n->params = *overrideParams;
    }
  };

  switch (presetIndex) {
  case 1:
    add("Exposure", nullptr);
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    add("Vignette", nullptr);
    break;
  case 2:
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
  case 3:
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
  case 4:
    add("Exposure", nullptr);
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 5:
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
  case 6:
    {
      const std::vector<float> tint = {0.35f, 1.05f, 0.92f, 0.85f};
      add("Tint", &tint);
    }
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 7:
    {
      const std::vector<float> tint = {0.35f, 0.85f, 0.95f, 1.05f};
      add("Tint", &tint);
    }
    add("Contrast", nullptr);
    add("Saturation", nullptr);
    break;
  case 8:
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

} // namespace Nyx
