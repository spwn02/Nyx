#pragma once

#include <string>

namespace Nyx {

class World;

class SceneSerializer {
public:
  static bool save(const std::string &path, World &world);
  static bool load(const std::string &path, World &world);
};

} // namespace Nyx
