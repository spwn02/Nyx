#include "PostGraph.h"
#include "post/PostGraphTypes.h"

namespace Nyx {

PostGraph::PostGraph() {
  PGNode &in = makeNode(PGNodeKind::Input, "Input");
  in.inPin = 0;
  ensurePins(in);
  m_inputNode = in.id;

  PGNode &out = makeNode(PGNodeKind::Output, "Output");
  out.outPin = 0;
  ensurePins(out);
  m_outputNode = out.id;

  // Default link Input->Output
  PGCompileError err{};
  tryAddLink(in.outPin, out.inPin, &err);
}

PGNode &PostGraph::makeNode(PGNodeKind kind, const char *name) {
  PGNode n{};
  n.id = m_ids.allocNode();
  n.kind = kind;
  n.name = name ? name : "Node";
  n.enabled = true;

  // Create pins
  n.inPin = m_ids.allocPin();
  n.outPin = m_ids.allocPin();
  m_pinOwner[n.inPin] = n.id;
  m_pinOwner[n.outPin] = n.id;

  m_nodes.push_back(std::move(n));
  return m_nodes.back();
}

void PostGraph::ensurePins(PGNode &n) {
  // Input node has only outPin; Output node only inPin
  if (n.kind == PGNodeKind::Input) {
    if (n.outPin == 0) {
      n.outPin = m_ids.allocPin();
      m_pinOwner[n.outPin] = n.id;
    }
    n.inPin = 0;
  } else if (n.kind == PGNodeKind::Output) {
    if (n.inPin == 0) {
      n.inPin = m_ids.allocPin();
      m_pinOwner[n.inPin] = n.id;
    }
    n.outPin = 0;
  } else {
    if (n.inPin == 0) {
      n.inPin = m_ids.allocPin();
      m_pinOwner[n.inPin] = n.id;
    }
    if (n.outPin == 0) {
      n.outPin = m_ids.allocPin();
      m_pinOwner[n.outPin] = n.id;
    }
  }
}

PGNode *PostGraph::findNode(PGNodeID id) {
  for (auto &n : m_nodes)
    if (n.id == id)
      return &n;
  return nullptr;
}

const PGNode *PostGraph::findNode(PGNodeID id) const {
  for (const auto &n : m_nodes)
    if (n.id == id)
      return &n;
  return nullptr;
}

PGNodeID PostGraph::addFilter(uint32_t typeID, const char *displayName,
                              const std::vector<float> &defaultParams) {
  PGNode &n =
      makeNode(PGNodeKind::Filter, displayName ? displayName : "Filter");
  n.typeID = typeID;
  n.params = defaultParams;
  ensurePins(n);

  // Insert before Output by default:
  // find prev->Output link and splice: prev -> new -> Output
  PGNode *out = findNode(m_outputNode);
  if (!out)
    return n.id;

  const PGNodeID prevNode = prevIntoInPin(out->inPin);
  const PGNode *prev = findNode(prevNode);

  // remove existing link prev->Output (if any)
  for (size_t i = 0; i < m_links.size(); ++i) {
    if (m_links[i].toPin == out->inPin) {
      m_links.erase(m_links.begin() + i);
      break;
    }
  }

  // add prev->new and new->out (best effort)
  PGCompileError err{};
  if (prev && prev->outPin)
    tryAddLink(prev->outPin, n.inPin, &err);
  tryAddLink(n.outPin, out->inPin, &err);

  return n.id;
}

void PostGraph::removeLink(PGLinkID linkID) {
  for (size_t i = 0; i < m_links.size(); ++i) {
    if (m_links[i].id == linkID) {
      m_links.erase(m_links.begin() + i);
      return;
    }
  }
}

void PostGraph::removeNode(PGNodeID nodeID) {
  PGNode *n = findNode(nodeID);
  if (!n)
    return;
  if (n->kind == PGNodeKind::Input || n->kind == PGNodeKind::Output)
    return; // can't remove fixed nodes

  // Chain splice: prev -> next
  const PGNodeID prevN = (n->inPin != 0) ? prevIntoInPin(n->inPin) : 0;
  const PGNodeID nextN = (n->outPin != 0) ? nextFromOutPin(n->outPin) : 0;

  // Remove links touching this node
  for (size_t i = 0; i < m_links.size();) {
    const bool touches =
        m_links[i].fromPin == n->outPin || m_links[i].toPin == n->inPin ||
        m_links[i].fromPin == n->inPin || m_links[i].toPin == n->outPin;
    if (touches)
      m_links.erase(m_links.begin() + i);
    else
      ++i;
  }

  // Remove node
  for (size_t i = 0; i < m_nodes.size(); ++i) {
    if (m_nodes[i].id == nodeID) {
      if (m_nodes[i].inPin)
        m_pinOwner.erase(m_nodes[i].inPin);
      if (m_nodes[i].outPin)
        m_pinOwner.erase(m_nodes[i].outPin);
      m_nodes.erase(m_nodes.begin() + i);
      break;
    }
  }

  // Reconnect prev->next if valid
  const PGNode *p = findNode(prevN);
  const PGNode *q = findNode(nextN);
  if (p && q && p->outPin && q->inPin) {
    PGCompileError err{};
    tryAddLink(p->outPin, q->inPin, &err);
  }
}

bool PostGraph::pinIsOutput(PGPinID p) const {
  auto it = m_pinOwner.find(p);
  if (it == m_pinOwner.end())
    return false;
  const PGNode *n = findNode(it->second);
  if (!n)
    return false;
  return n->outPin == p;
}
bool PostGraph::pinIsInput(PGPinID p) const {
  auto it = m_pinOwner.find(p);
  if (it == m_pinOwner.end())
    return false;
  const PGNode *n = findNode(it->second);
  if (!n)
    return false;
  return n->inPin == p;
}

bool PostGraph::hasIncoming(PGPinID inPin) const {
  for (const auto &l : m_links)
    if (l.toPin == inPin)
      return true;
  return false;
}
bool PostGraph::hasOutgoing(PGPinID outPin) const {
  for (const auto &l : m_links)
    if (l.fromPin == outPin)
      return true;
  return false;
}

PGNodeID PostGraph::nextFromOutPin(PGPinID outPin) const {
  for (const auto &l : m_links) {
    if (l.fromPin == outPin) {
      auto it = m_pinOwner.find(l.toPin);
      return (it == m_pinOwner.end()) ? 0 : it->second;
    }
  }
  return 0;
}

PGNodeID PostGraph::prevIntoInPin(PGPinID inPin) const {
  for (const auto &l : m_links) {
    if (l.toPin == inPin) {
      auto it = m_pinOwner.find(l.fromPin);
      return (it == m_pinOwner.end()) ? 0 : it->second;
    }
  }
  return 0;
}

bool PostGraph::wouldCreateCycle(PGNodeID fromNode, PGNodeID toNode) const {
  // In chain graph, cycle exists if walking forward from toNode can reach
  // fromNode
  const PGNode *start = findNode(toNode);
  if (!start)
    return false;

  PGNodeID cur = toNode;
  for (int guard = 0; guard < 2048; ++guard) {
    if (cur == 0)
      break;
    if (cur == fromNode)
      return true;

    const PGNode *n = findNode(cur);
    if (!n || n->outPin == 0)
      break;
    cur = nextFromOutPin(n->outPin);
  }
  return false;
}

bool PostGraph::tryAddLink(PGPinID fromOutPin, PGPinID toInPin,
                           PGCompileError *err) {
  if (err) {
    err->ok = false;
    err->message.clear();
  }

  if (!pinIsOutput(fromOutPin) || !pinIsInput(toInPin)) {
    if (err)
      err->message = "Must connect Output pin to Input pin.";
    return false;
  }

  const auto itA = m_pinOwner.find(fromOutPin);
  const auto itB = m_pinOwner.find(toInPin);
  if (itA == m_pinOwner.end() || itB == m_pinOwner.end()) {
    if (err)
      err->message = "Invalid pins.";
    return false;
  }

  const PGNodeID aNode = itA->second;
  const PGNodeID bNode = itB->second;

  if (aNode == bNode) {
    if (err)
      err->message = "Cannot link node to itself.";
    return false;
  }

  // Chain rule: only one outgoing per output pin, one incoming per input pin
  if (hasOutgoing(fromOutPin)) {
    if (err)
      err->message = "This output is already connected.";
    return false;
  }
  if (hasIncoming(toInPin)) {
    if (err)
      err->message = "This input is already connected.";
    return false;
  }

  // No cycles
  if (wouldCreateCycle(aNode, bNode)) {
    if (err)
      err->message = "Cycle not allowed.";
    return false;
  }

  PGLink link{};
  link.id = m_ids.allocLink();
  link.fromPin = fromOutPin;
  link.toPin = toInPin;
  m_links.push_back(link);

  if (err)
    err->ok = true;
  return true;
}

PGCompileError PostGraph::buildChainOrder(std::vector<PGNodeID> &outOrder) const {
  outOrder.clear();

  const PGNode *input = findNode(m_inputNode);
  const PGNode *output = findNode(m_outputNode);
  if (!input || !output) {
    return {false, "Graph missing input or output node."};
  }
  if (input->outPin == 0 || output->inPin == 0) {
    return {false, "Graph has invalid endpoint pins."};
  }

  PGNodeID cur = nextFromOutPin(input->outPin);
  if (cur == 0) {
    return {false, "Graph is not connected."};
  }

  std::unordered_map<PGNodeID, bool> visited;
  for (int guard = 0; guard < 4096; ++guard) {
    if (cur == m_outputNode)
      return {true, {}};

    if (visited[cur])
      return {false, "Cycle detected."};
    visited[cur] = true;

    const PGNode *n = findNode(cur);
    if (!n)
      return {false, "Broken node link."};

    if (n->kind == PGNodeKind::Filter)
      outOrder.push_back(cur);

    if (n->outPin == 0)
      return {false, "Broken chain."};

    cur = nextFromOutPin(n->outPin);
    if (cur == 0)
      return {false, "Graph is not connected."};
  }

  return {false, "Chain traversal exceeded guard."};
}

} // namespace Nyx
