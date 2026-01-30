#pragma once
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include "scene/EntityID.h"

namespace Nyx {

class World;

using IsEntitySelectedFn = std::function<bool(EntityID e)>;

struct CameraOverlaySettings final {
  bool showAllCameras = false;
  bool hideActiveCamera = false;
  EntityID hideEntity = InvalidEntity;
  float iconSizePx = 10.0f;
  float frustumLineThickness = 2.0f;
  float frustumDepth = 2.0f;
};

class CameraGizmosOverlay final {
public:
  void draw(World &world, const glm::mat4 &editorViewProj,
            const glm::vec2 &viewportImageMin,
            const glm::vec2 &viewportImageMax, IsEntitySelectedFn isSelected,
            const CameraOverlaySettings &settings = {});

private:
  static bool projectToScreen(const glm::mat4 &viewProj,
                              const glm::vec3 &worldPos,
                              const glm::vec2 &imgMin,
                              const glm::vec2 &imgMax,
                              glm::vec2 &outScreen);

  void drawCameraIcon(const glm::vec2 &p, float sizePx, uint32_t fillColor,
                      uint32_t outlineColor);
};

} // namespace Nyx
