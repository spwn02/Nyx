#pragma once
#include "ui/GizmoState.h"
#include "render/ViewMode.h"
#include "scene/EntityUUID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct PanelState final {
  bool showHierarchy = true;
  bool showInspector = true;
  bool showViewport = true;
  bool showAssets = false;
  bool showStats = true;
  bool showConsole = false;
  bool showGraph = false;
};

struct EditorViewportPrefs final {
  bool showGrid = true;
  bool showGizmos = true;
  bool showSelectionOutline = true;

  uint32_t msaa = 1;
  float exposure = 0.0f;

  ViewMode viewMode = ViewMode::Lit;
};

struct EditorState final {
  std::string lastProjectPath;
  std::string lastScenePath;
  std::vector<std::string> recentScenes;
  bool autoSave = false;

  EntityUUID activeCamera = {};

  GizmoOp gizmoOp = GizmoOp::Translate;
  GizmoMode gizmoMode = GizmoMode::Local;

  PanelState panels{};
  EditorViewportPrefs viewport{};

  bool dockFallbackApplied = false;

  uint64_t uuidSeed = 0x12345678ABCDEF01ULL;

  void pushRecentScene(const std::string &path);
};

} // namespace Nyx
