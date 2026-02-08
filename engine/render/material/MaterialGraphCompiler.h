#pragma once

#include "MaterialGraph.h"
#include "MaterialGraphVM.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace Nyx {

struct CompiledMaterialGraph final {
  GpuMatGraphHeader header{};
  std::vector<GpuMatNode> nodes; // linear VM program
};

struct MatCompilerError final {
  std::string msg;
};

struct MaterialGraphCompiler final {
public:
  // Compile graph to VM.
  // - Produces linear nodes with register assignment.
  // - NormalMap outputs WORLD normal (uses TBN)
  // - OutputSurface writes header output regs
  bool compile(const MaterialGraph &g, CompiledMaterialGraph &out,
               MatCompilerError *err = nullptr);

private:
  struct NodeInfo {
    glm::uvec4 outReg{0}; // for multi-outputs if needed
    bool compiled = false;
  };

  // link resolver: (to.node, to.slot) - from pin
  struct PinKey {
    MatNodeID node;
    uint32_t slot;
    bool operator==(const PinKey &other) const {
      return node == other.node && slot == other.slot;
    }
  };

  struct PinKeyHash {
    size_t operator()(const PinKey &k) const noexcept {
      return (size_t(k.node) * 1315423911u) ^ (size_t(k.slot) * 2654435761u);
    }
  };

  std::unordered_map<MatNodeID, const MatNode *> m_nodes;
  std::unordered_map<PinKey, MatPin, PinKeyHash> m_incoming;

  std::unordered_map<MatNodeID, NodeInfo> m_info;

  uint32_t m_nextReg = 0;
  bool m_outSet = false;
  uint32_t m_outBase = 0;
  uint32_t m_outMetal = 0;
  uint32_t m_outRough = 0;
  uint32_t m_outNormal = 1;
  uint32_t m_outAO = 0;
  uint32_t m_outEmis = 0;
  uint32_t m_outAlpha = 0;

  uint32_t allocReg();
  uint32_t ensureInputReg(const MatNode &node, uint32_t inputSlot,
                          uint32_t defaultKind, const glm::vec4 &defaultV4,
                          std::vector<GpuMatNode> &prog, MatCompilerError *err);
  uint32_t compileNode(const MatNode &n, uint32_t outSlot,
                       std::vector<GpuMatNode> &prog, MatCompilerError *err);

  const MatNode *findNode(MatNodeID id) const;

  void setError(MatCompilerError *err, const char *msg);
};

} // namespace Nyx
