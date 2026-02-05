#pragma once

#include "PostGraphTypes.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx {

struct PGNode final {
  PGNodeID id = 0;
  PGNodeKind kind = PGNodeKind::Filter;

  // Filter only:
  uint32_t typeID = 0;
  std::string name;
  bool enabled = true;
  std::vector<float> params; // matches registry paramCount
  std::string lutPath;        // optional LUT path for LUT filters

  // Node editor state
  float posX = 0.0f;
  float posY = 0.0f;

  // Pins
  PGPinID inPin = 0;  // 0 if none (Input node)
  PGPinID outPin = 0; // 0 if none (Output node)
};

struct PGLink final {
  PGLinkID id = 0;
  PGPinID fromPin = 0; // output pin
  PGPinID toPin = 0;   // input pin
};

struct PGCompileError final {
  bool ok = true;
  std::string message;
};

struct FilterStackCPU final {
  struct Entry {
    uint32_t typeID = 0;
    uint32_t enabled = 1;
    glm::vec4 p0{};
    glm::vec4 p1{};
  };

  std::vector<Entry> entries;
};

class PostGraph final {
public:
  PostGraph();

  // Fixed nodes
  PGNodeID inputNode() const { return m_inputNode; }
  PGNodeID outputNode() const { return m_outputNode; }

  // Filter ops
  PGNodeID addFilter(uint32_t typeID, const char *displayName,
                     const std::vector<float> &defaulParams);
  void removeNode(PGNodeID nodeID);

  // Link ops (CHAIN rules enforced)
  bool tryAddLink(PGPinID fromOutPin, PGPinID toInPin, PGCompileError *err);
  void removeLink(PGLinkID linkID);

  // Graph queries
  const std::vector<PGNode> &nodes() const { return m_nodes; }
  const std::vector<PGLink> &links() const { return m_links; }

  PGNode *findNode(PGNodeID id);
  const PGNode *findNode(PGNodeID id) const;

  // Compile to chain (Input -> Output). Does not touch GPU.
  PGCompileError compileChain(const class FilterRegistry &reg,
                              FilterStackCPU &out) const;

  // Convienience: build current linear order of node IDs (filters only)
  PGCompileError buildChainOrder(std::vector<PGNodeID> &outOrder) const;

private:
  PGIDGen m_ids{};

  PGNodeID m_inputNode = 0;
  PGNodeID m_outputNode = 0;

  std::vector<PGNode> m_nodes;
  std::vector<PGLink> m_links;

  // pin->node
  std::unordered_map<PGPinID, PGNodeID> m_pinOwner;

  PGNode &makeNode(PGNodeKind kind, const char *name);
  void ensurePins(PGNode &n);

  // Chain helpers
  bool pinIsOutput(PGPinID p) const;
  bool pinIsInput(PGPinID p) const;

  bool hasIncoming(PGPinID inPin) const;
  bool hasOutgoing(PGPinID outPin) const;

  // Returns node connected from outPin (next), or 0.
  PGNodeID nextFromOutPin(PGPinID outPin) const;
  // Returns node connected to inPin (prev), or 0.
  PGNodeID prevIntoInPin(PGPinID inPin) const;

  bool wouldCreateCycle(PGNodeID fromNode, PGNodeID toNode) const;
};

} // namespace Nyx
