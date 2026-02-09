#pragma once

#include "scene/NyxScene.h"

#include <string>

namespace Nyx {

class NyxSceneIO {
public:
  static bool save(const NyxScene &scene, const std::string &path,
                   std::string *outError = nullptr);

  static bool load(NyxScene &outScene, const std::string &path,
                   std::string *outError = nullptr);
};

} // namespace Nyx
