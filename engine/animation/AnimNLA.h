#pragma once

#include "AnimationTypes.h"
#include "scene/EntityID.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

struct AnimActionTrack final {
  AnimChannel channel = AnimChannel::TranslateX;
  AnimCurve curve{};
};

struct AnimAction final {
  std::string name{"Action"};
  AnimFrame start = 0;
  AnimFrame end = 0;
  std::vector<AnimActionTrack> tracks;
};

using ActionID = uint32_t;

enum class NlaBlendMode : uint8_t {
  Replace = 0,
  Add,
};

struct NlaStrip final {
  ActionID action = 0;
  EntityID target = InvalidEntity;

  // Global range where strip is active (inclusive).
  AnimFrame start = 0;
  AnimFrame end = 0;

  // Local action range.
  AnimFrame inFrame = 0;
  AnimFrame outFrame = 0;

  // Playback remap.
  float timeScale = 1.0f;
  bool reverse = false;

  // Blending.
  NlaBlendMode blend = NlaBlendMode::Replace;
  float influence = 1.0f;

  // Fade in/out measured in global frames.
  AnimFrame fadeIn = 0;
  AnimFrame fadeOut = 0;

  // Higher layer is applied later.
  int32_t layer = 0;

  bool muted = false;
};

} // namespace Nyx

