#pragma once

#include "project/NyxProjectRuntime.h"
#include <string>

namespace Nyx::AssetOps {

bool createFolder(NyxProjectRuntime &proj, const std::string &folderRel);
bool createEmptyTextFile(NyxProjectRuntime &proj, const std::string &fileRel,
                         const char *text);

} // namespace Nyx::AssetOps
