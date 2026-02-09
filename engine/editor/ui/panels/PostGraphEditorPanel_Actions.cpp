#include "PostGraphEditorPanel.h"

#include <imgui_node_editor.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ed = ax::NodeEditor;

namespace Nyx {

void PostGraphEditorPanel::onDeleteSelection(PostGraph &graph) {
  ed::SetCurrentEditor(m_ctx);

  {
    std::vector<ed::LinkId> links;
    links.resize(ed::GetSelectedObjectCount());
    const int linkCount = ed::GetSelectedLinks(links.data(), links.size());
    for (int i = 0; i < linkCount; ++i) {
      const PGLinkID lid = static_cast<PGLinkID>(
          reinterpret_cast<uintptr_t>(links[i].AsPointer()));
      graph.removeLink(lid);
      markChanged();
    }
  }

  {
    std::vector<ed::NodeId> nodes;
    nodes.resize(ed::GetSelectedObjectCount());
    const int nodeCount = ed::GetSelectedNodes(nodes.data(), nodes.size());
    for (int i = 0; i < nodeCount; ++i) {
      const PGNodeID nid = static_cast<PGNodeID>(
          reinterpret_cast<uintptr_t>(nodes[i].AsPointer()));
      if (nid == graph.inputNode() || nid == graph.outputNode())
        continue;
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
    const PGNodeID nid = static_cast<PGNodeID>(
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

  std::sort(toRemove.begin(), toRemove.end());
  toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());

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
  toRemove.erase(std::unique(toRemove.begin(), toRemove.end()), toRemove.end());
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
