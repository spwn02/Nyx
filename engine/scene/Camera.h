#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx {

// “Scene camera” component (not the editor camera).
enum class CameraProjection : uint8_t {
  Perspective = 0,
  Orthographic = 1,
};

struct CCamera final {
  CameraProjection projection = CameraProjection::Perspective;

  float fovYDeg = 60.0f;
  float orthoHeight = 10.0f;

  float nearZ = 0.01f;
  float farZ = 2000.0f;

  // Film / DoF controls
  float aperture = 2.8f;      // f-number
  float focusDistance = 10.0f; // meters
  float sensorWidth = 36.0f;   // mm
  float sensorHeight = 24.0f;  // mm

  // Blender-ish editor control convenience (optional, but useful for later):
  float exposure = 0.0f; // EV-like; can map to tonemap later
  bool dirty = true;
};

struct CCameraMatrices final {
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
  glm::mat4 viewProj{1.0f};
  bool dirty = true;
  uint32_t lastW = 0;
  uint32_t lastH = 0;
};

} // namespace Nyx
