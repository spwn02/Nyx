#pragma once

#include "post/FilterRegistry.h"
#include "post/PostGraph.h"

#include <imgui.h>
#include <unordered_map>
#include <unordered_set>

namespace ax::NodeEditor {
struct EditorContext;
}

namespace Nyx {

class PostGraphEditorPanel final {
public:
  PostGraphEditorPanel();
  ~PostGraphEditorPanel();

  void draw(PostGraph &graph, const FilterRegistry &registry,
            class EngineContext &engine);
  bool isHovered() const { return m_isHovered; }

  bool consumeGraphChanged() {
    const bool v = m_graphChanged;
    m_graphChanged = false;
    return v;
  }

private:
  ax::NodeEditor::EditorContext *m_ctx = nullptr;

  bool m_graphChanged = false;
  bool m_openAddMenu = false;
  char m_search[128]{};
  bool m_requestAutoLayout = false;
  bool m_requestNavigateToContent = true;
  bool m_initialZoomPending = true;
  bool m_initialZoomArmed = false;
  std::unordered_set<PGNodeID> m_initializedNodes{};
  bool m_isHovered = false;
  bool m_requestOpenAddMenu = false;
  int m_presetIndex = 0;
  double m_lastEditCommit = 0.0;
  PGNodeID m_ctrlDragNodeId = 0;
  bool m_ctrlDragActive = false;
  float m_lastDrawMs = 0.0f;

  // UI clipboard for node params
  uint32_t m_clipTypeID = 0;
  std::vector<float> m_clipParams{};

  // Remember last hovered screen position for popup placement
  ImVec2 m_popupPos{0.0f, 0.0f};

  void drawAddMenu(PostGraph &graph, const FilterRegistry &registry);
  void drawNodeContents(PostGraph &graph, const FilterRegistry &registry,
                        EngineContext &engine, PGNode &node);
  void autoLayout(PostGraph &graph);
  void applyPreset(PostGraph &graph, const FilterRegistry &registry,
                   int presetIndex);

  void onDeleteSelection(PostGraph &graph);
  void onUnlinkSelection(PostGraph &graph);
  void unlinkNode(PostGraph &graph, PGNodeID nodeId);
  void tryInsertNodeIntoLink(PostGraph &graph, PGNodeID nodeId, PGLinkID linkId);
  void markChanged() { m_graphChanged = true; }

  struct PinScreenData {
    ImVec2 pos{0.0f, 0.0f};
    bool isOutput = false;
  };
  std::unordered_map<PGPinID, PinScreenData> m_pinScreenCache{};
};

} // namespace Nyx
