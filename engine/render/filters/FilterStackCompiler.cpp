#include "FilterStackCompiler.h"
#include "post/FilterGraph.h"
#include "post/FilterRegistry.h"
#include "render/filters/FilterStackGPU.h"

#include <algorithm>
#include <cstring>

namespace Nyx {

static inline uint32_t umin(uint32_t a, uint32_t b) { return (a < b) ? a : b; }

CompiledFilterStack FilterStackCompiler::compile(const FilterGraph &g) const {
  // Count enabled nodes (still upload disabled too).
  const uint32_t n = g.nodes().size();

  const size_t headerSize = sizeof(GpuFilterStackHeader);
  const size_t nodeSize = sizeof(GpuFilterNode);
  const size_t totalSize = headerSize + n * nodeSize;

  CompiledFilterStack out{};
  out.nodeCount = n;
  out.bytes.resize(totalSize);

  // header
  GpuFilterStackHeader hdr{};
  hdr.count = n;
  std::memcpy(out.bytes.data(), &hdr, sizeof(hdr));

  // nodes
  uint8_t *dst = out.bytes.data() + headerSize;
  for (uint32_t i = 0; i < n; ++i) {
    const FilterNode &srcN = g.nodes()[i];

    GpuFilterNode gn{};
    gn.type = srcN.type;
    gn.enabled = srcN.enabled ? 1u : 0u;

    const FilterTypeInfo *ti = m_reg.find(srcN.type);
    if (!ti) {
      // unknown type - disabled no-op
      gn.enabled = 0u;
      gn.paramCount = 0u;
      std::memset(gn.params, 0, sizeof(gn.params));
    } else {
      const uint32_t pc =
          umin(ti->gpuParamCount ? ti->gpuParamCount : ti->paramCount,
               kGpuFilterMaxParams);
      gn.paramCount = pc;
      for (uint32_t p = 0; p < pc; ++p) {
        gn.params[p] = srcN.params[p];
      }
      // remaining already 0
    }

    std::memcpy(dst + i * nodeSize, &gn, sizeof(gn));
  }

  return out;
}

// FNV-1a 64-bit
uint64_t FilterStackCompiler::hashBytes(const std::vector<uint8_t> &b) {
  uint64_t h = 14695981039346656037ULL;
  for (uint8_t byte : b) {
    h ^= static_cast<uint64_t>(byte);
    h *= 1099511628211ULL;
  }
  return h;
}

} // namespace Nyx
