#pragma once

#include "EditorCamera.h"
#include "editor/GizmoState.h"
#include <expected>
#include <string>
#include <unordered_map>

namespace Nyx {

struct EditorPanels final {
  bool viewport = true;
  bool hierarchy = true;
  bool inspector = true;
  bool assetBrowser = true;
  bool stats = false;
  bool renderSettings = false;
  bool projectSettings = false;
};

struct EditorPersistState final {
  EditorCamera camera{};

  GizmoOp gizmoOp = GizmoOp::Translate;
  GizmoMode gizmoMode = GizmoMode::Local;

  EditorPanels panels{};

  // Dock layout fallback
  int dockLayoutVersion = 1;
  bool dockLayoutApplied = false; // runtime flag
};

struct EditorPersist final {
  static std::expected<void, std::string> save(const std::string &path,
                                               const EditorPersistState &s);
  static std::expected<void, std::string> load(const std::string &path,
                                               EditorPersistState &out);

private:
  static std::unordered_map<std::string, std::string>
  parseKV(const std::string &text);
  static std::string trim(std::string v);

  static bool toBool(const std::string &v, bool def);
  static int toInt(const std::string &v, int def);
  static float toFloat(const std::string &v, float def);
};

} // namespace Nyx
