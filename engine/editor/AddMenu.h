#pragma once

#include "Selection.h"
#include "scene/World.h"

namespace Nyx {

class AddMenu final {
public:
  // call every frame; opens on Shift+A
  void tick(World &world, Selection &sel, bool allowOpen);

private:
  void spawn(World &world, Selection &sel, ProcMeshType t);

private:
  bool m_open = false;
  char m_filter[64]{};
};

} // namespace Nyx
