#pragma once
#include <string>

namespace Nyx {

struct SceneRuntime final {
  std::string pathAbs; // absolute .nyxscene
  std::string pathRel; // relative to project root
  bool dirty = false;
};

} // namespace Nyx
