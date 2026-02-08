#pragma once

#include "editor/Selection.h"
#include "scene/World.h"

namespace Nyx {

class EngineContext;
class SequencerPanel;

class InspectorPanel {
public:
  void draw(World &world, EngineContext &engine, Selection &sel,
            SequencerPanel *sequencer);
};

} // namespace Nyx
