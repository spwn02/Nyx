#pragma once

#include <cstdint>

namespace Nyx {

using PGNodeID = uint32_t;
using PGPinID = uint32_t;
using PGLinkID = uint32_t;

enum class PGNodeKind : uint8_t { Input, Output, Filter };
enum class PGPinKind : uint8_t { Input, Output };

// Helper to generate stable per-graph ids.
struct PGIDGen final {
  uint32_t nextNodeID = 1;
  uint32_t nextPinID = 1000;
  uint32_t nextLink = 100000;

  PGNodeID allocNode() { return nextNodeID++; }
  PGPinID allocPin() { return nextPinID++; }
  PGLinkID allocLink() { return nextLink++; }
};

} // namespace Nyx
