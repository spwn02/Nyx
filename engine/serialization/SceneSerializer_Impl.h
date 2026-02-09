#pragma once

#include <string>

namespace Nyx {

class World;

namespace detail {

bool saveSceneBinary(const std::string &path, World &world);
bool loadSceneBinary(const std::string &path, World &world);

} // namespace detail

} // namespace Nyx
