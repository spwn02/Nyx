#pragma once

#include "editor/Selection.h"
#include "scene/World.h"

namespace Nyx {

class InspectorLight final {
public:
  bool draw(World &world, Selection &sel);
};

} // namespace Nyx
