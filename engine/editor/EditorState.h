#pragma once
#include "animation/AnimationTypes.h"
#include "editor/SequencerState.h"
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
  float outlineThicknessPx = 1.5f;

  ViewMode viewMode = ViewMode::Lit;
};

struct PersistedAnimTrack final {
  EntityUUID entity = {};
  uint32_t blockId = 0;
  AnimChannel channel{};
  AnimCurve curve{};
};

struct PersistedAnimRange final {
  EntityUUID entity = {};
  uint32_t blockId = 0;
  AnimFrame start = 0;
  AnimFrame end = 0;
};

struct PersistedAnimationClip final {
  bool valid = false;
  std::string name;
  AnimFrame lastFrame = 160;
  bool loop = true;
  std::vector<PersistedAnimTrack> tracks;
  std::vector<PersistedAnimRange> ranges;
  uint32_t nextBlockId = 1;
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

  float projectFps = 30.0f;
  int32_t animationFrame = 0;
  bool animationPlaying = false;
  bool animationLoop = true;
  int32_t animationLastFrame = 160;
  PersistedAnimationClip animationClip{};
  SequencerPersistState sequencer{};
  uint64_t uuidSeed = 0x12345678ABCDEF01ULL;

  void pushRecentScene(const std::string &path);
};

} // namespace Nyx
