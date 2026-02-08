#pragma once

#include "NyxBinaryReader.h"
#include "NyxBinaryWriter.h"
#include "NyxChunkIDs.h"
#include "NyxStringTable.h"

namespace Nyx {

class World;

class SceneSerializer {
public:
  static bool save(const std::string &path, World &world);
  static bool load(const std::string &path, World &world);
};

} // namespace Nyx
