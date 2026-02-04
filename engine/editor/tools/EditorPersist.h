#pragma once

#include "editor/ui/GizmoState.h"
#include <expected>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Nyx {

struct EditorCameraPersist final {
  glm::vec3 position{0.0f, 1.5f, 3.0f};
  float yawDeg = -90.0f;
  float pitchDeg = 0.0f;

  float fovYDeg = 60.0f;
  float nearZ = 0.01f;
  float farZ = 2000.0f;

  float speed = 6.0f;
  float boostMul = 2.0f;
  float sensitivity = 0.12f; // deg per pixel (tweakable)
};

struct EditorPanels final {
  bool viewport = true;
  bool hierarchy = true;
  bool inspector = true;
  bool sky = true;
  bool assetBrowser = true;
  bool stats = false;
  bool renderSettings = false;
  bool projectSettings = false;
};

struct EditorPersistState final {
  EditorCameraPersist camera{};

  GizmoOp gizmoOp = GizmoOp::Translate;
  GizmoMode gizmoMode = GizmoMode::Local;
  bool gizmoUseSnap = false;
  float gizmoSnapTranslate = 0.5f;
  float gizmoSnapRotateDeg = 15.0f;
  float gizmoSnapScale = 0.1f;

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
