#pragma once

#include <string>

namespace Nyx {

class NyxProjectRuntime;

void drawAssetBrowserContextMenu(NyxProjectRuntime &proj,
                                 const std::string &currentFolderRel,
                                 bool *outDoRescan);

} // namespace Nyx
