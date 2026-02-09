#include "SceneSerializer.h"

#include "SceneSerializer_Impl.h"

namespace Nyx {

bool SceneSerializer::save(const std::string &path, World &world) {
  return detail::saveSceneBinary(path, world);
}

bool SceneSerializer::load(const std::string &path, World &world) {
  return detail::loadSceneBinary(path, world);
}

} // namespace Nyx
