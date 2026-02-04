#pragma once

#include "editor/Selection.h"
#include "editor/tools/ViewportProjector.h"
#include "scene/World.h"

namespace Nyx {

class LightGizmosOverlay final {
public:
  void draw(World &world, Selection &sel, const ViewportProjector &proj);
};

} // namespace Nyx
