#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct NyxSceneHeader final {
  uint32_t versionMajor = 1;
  uint32_t versionMinor = 1;
};

using SceneEntityID = uint64_t;

struct SceneTransform final {
  float tx = 0.0f;
  float ty = 0.0f;
  float tz = 0.0f;
  float rx = 0.0f;
  float ry = 0.0f;
  float rz = 0.0f;
  float rw = 1.0f;
  float sx = 1.0f;
  float sy = 1.0f;
  float sz = 1.0f;
};

struct SceneHierarchy final {
  SceneEntityID parent = 0;
};

struct SceneCamera final {
  float fovY = 60.0f;
  float nearZ = 0.01f;
  float farZ = 2000.0f;
  float aperture = 2.8f;
  float focusDistance = 10.0f;
  float sensorWidth = 36.0f;
  float sensorHeight = 24.0f;
  bool active = false;
};

enum class SceneLightType : uint8_t {
  Directional,
  Point,
  Spot
};

struct SceneLight final {
  SceneLightType type = SceneLightType::Point;
  float color[3] = {1.0f, 1.0f, 1.0f};
  float intensity = 10.0f;
  float range = 5.0f;
  float spotAngle = 0.0f;
};

struct SceneRenderable final {
  std::string meshAsset;
  std::string materialAsset;
};

struct SceneEntity final {
  SceneEntityID id = 0;
  std::string name;

  SceneTransform transform{};
  SceneHierarchy hierarchy{};

  bool hasCamera = false;
  SceneCamera camera{};

  bool hasLight = false;
  SceneLight light{};

  bool hasRenderable = false;
  SceneRenderable renderable{};
};

struct NyxScene final {
  NyxSceneHeader header{};
  std::string name;

  std::vector<SceneEntity> entities;

  std::string skyAsset;
  float exposure = 1.0f;
};

} // namespace Nyx
