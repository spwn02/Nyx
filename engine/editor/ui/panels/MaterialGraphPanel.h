#pragma once

#include "material/MaterialHandle.h"
#include "render/material/MaterialGraph.h"
#include <imgui.h>
#include <unordered_set>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace Nyx {

class EngineContext;

class MaterialGraphPanel final {
public:
  void setMaterial(MaterialHandle h);
  void draw(EngineContext &engine);
  bool isHovered() const { return m_hovered; }

private:
  void ensureContext();
  void ensureDefaultGraph(EngineContext &engine);
  void drawToolbar(EngineContext &engine);
  void drawGraph(EngineContext &engine);
  void drawNodeProps(EngineContext &engine);
  void drawAddMenu(EngineContext &engine);

  MaterialHandle m_mat{};
  ax::NodeEditor::EditorContext *m_ctx = nullptr;

  // UI state
  char m_search[128]{};
  bool m_openAddMenu = false;
  bool m_requestOpenAddMenu = false;
  bool m_requestAutoLayout = false;
  bool m_requestNavigateToContent = false;
  ImVec2 m_popupPos{0.0f, 0.0f};
  bool m_hovered = false;

  uint32_t m_selectedNode = 0;
  uint32_t m_selectedLink = 0;

  // clipboard
  bool m_hasClipboard = false;
  MaterialGraph m_clipboard{};

  std::unordered_set<uint32_t> m_posInitialized{};
  float m_lastDrawMs = 0.0f;
};

} // namespace Nyx
