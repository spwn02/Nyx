#pragma once
#include "scene/EntityID.h"
#include "scene/EntityUUID.h"
#include <string>

namespace Nyx {

class World;
class MaterialSystem;

// Contract:
// - save: deterministic ordering by UUID
// - load: creates entities w/ UUID, then hierarchy, then components
struct WorldSerializer final {
  // Returns false on I/O failure
  static bool saveToFile(const World &world, EntityID editorCamera,
                         const std::string &path);
  static bool saveToFile(const World &world, EntityID editorCamera,
                         const MaterialSystem &materials,
                         const std::string &path);

  // Returns false on parse/validation failure
  static bool loadFromFile(World &world, const std::string &path);
  static bool loadFromFile(World &world, MaterialSystem &materials,
                           const std::string &path);
};

} // namespace Nyx
