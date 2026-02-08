#pragma once

#include "NyxBinaryReader.h"
#include "NyxBinaryWriter.h"
#include "NyxProject.h"

namespace Nyx {

class ProjectSerializer {
public:
  static bool save(const std::string &path, const NyxProject &project);
  static bool load(const std::string &path, NyxProject &project);
};

} // namespace Nyx
