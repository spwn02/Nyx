#pragma once

#include "editor/Selection.h"
#include "editor/EditorState.h"
#include "scene/EntityID.h"
#include <memory>

namespace Nyx {

class AppContext;
class EngineContext;

class Application final {
public:
  Application(std::unique_ptr<AppContext> app,
              std::unique_ptr<EngineContext> engine);
  ~Application();

  int run();

private:
  std::unique_ptr<AppContext> m_app;
  std::unique_ptr<EngineContext> m_engine;

  bool m_pendingViewportPick = false;
  bool m_pendingPickCtrl = false;
  bool m_pendingPickShift = false;
  EntityID m_lastViewportPicked = InvalidEntity;

  // Cycling state (for repeated clicks without Ctrl/Shift)
  EntityID m_cycleRoot = InvalidEntity;
  uint32_t m_cycleIndex = 0;
  Selection m_cycleLastSel{};
  EditorState m_editorState{};
};

} // namespace Nyx
