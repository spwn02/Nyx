#pragma once

#include "editor/EditorHistory.h"

namespace Nyx {

class HistoryPanel final {
public:
  void draw(EditorHistory &history, class World &world,
            class MaterialSystem &materials, class Selection &sel,
            class EngineContext &engine);
};

} // namespace Nyx
