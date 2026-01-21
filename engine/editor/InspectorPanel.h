#pragma once

#include "Selection.h"
#include "scene/World.h"

namespace Nyx {

class EngineContext;

class InspectorPanel {
public:
  void draw(World &world, EngineContext &engine, Selection &sel);
};

} // namespace Nyx
