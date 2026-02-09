#include "NyxProjectIO.h"

#include "NyxProj.h"
#include "NyxProjIO.h"

namespace Nyx {

bool NyxProjectIO::loadProject(const char *absPath, NyxProjectDesc &out) {
  out = NyxProjectDesc{};
  if (!absPath || !absPath[0])
    return false;

  auto loaded = NyxProjIO::load(absPath);
  if (!loaded)
    return false;

  out.projectName = loaded->proj.name;
  out.startSceneRel = loaded->proj.settings.startupScene;
  return true;
}

bool NyxProjectIO::saveProject(const char *absPath, const NyxProjectDesc &desc) {
  if (!absPath || !absPath[0])
    return false;

  NyxProject proj{};
  proj.name = desc.projectName.empty() ? "NyxProject" : desc.projectName;
  proj.assetRootRel = "Content";
  proj.settings.exposure = 1.0f;
  proj.settings.vsync = true;
  proj.settings.startupScene = desc.startSceneRel.empty()
                                   ? "Content/Scenes/Main.nyxscene"
                                   : desc.startSceneRel;

  return NyxProjIO::save(absPath, proj);
}

} // namespace Nyx
