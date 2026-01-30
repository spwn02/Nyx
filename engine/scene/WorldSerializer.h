#pragma once
#include "scene/EntityUUID.h"
#include <string>

namespace Nyx {

class World;

// Contract:
// - save: deterministic ordering by UUID
// - load: creates entities w/ UUID, then hierarchy, then components
struct WorldSerializer final {
  // Returns false on I/O failure
  static bool saveToFile(const World &world, const std::string &path);

  // Returns false on parse/validation failure
  static bool loadFromFile(World &world, const std::string &path);
};

} // namespace Nyx
