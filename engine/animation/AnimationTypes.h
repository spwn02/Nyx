#pragma once

#include "scene/EntityID.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Nyx {

// Frame index (integer timeline)
using AnimFrame = int32_t;

// Interpolation
enum class InterpMode : uint8_t {
  Constant,
  Linear,
  Bezier,
};

// Optional per-segment easing preset (applies from this key to next key).
enum class SegmentEase : uint8_t {
  None = 0,
  CubicIn,
  CubicOut,
  CubicInOut,
  QuintIn,
  QuintOut,
  QuintInOut,
  ExponentialIn,
  ExponentialOut,
  ExponentialInOut,
  BackIn,
  BackOut,
  BackInOut,
  BounceIn,
  BounceOut,
  BounceInOut,
};

// Tangent (for Bezier)
struct AnimTangent {
  float dx = 0.0f;
  float dy = 0.0f;
};

// Keyframe
struct AnimKey {
  AnimFrame frame = 0;
  float value = 0.0f;

  AnimTangent in;
  AnimTangent out;
  SegmentEase easeOut = SegmentEase::None;
};

// Curve (1D)
struct AnimCurve {
  InterpMode interp = InterpMode::Linear;
  std::vector<AnimKey> keys;

  // Assumes keys are sorted by frame
  float sample(AnimFrame frame) const;
};

// What property is animated
enum class AnimChannel : uint8_t {
  // Transform
  TranslateX,
  TranslateY,
  TranslateZ,

  RotateX,
  RotateY,
  RotateZ, // stored as Euler, converted to quat

  ScaleX,
  ScaleY,
  ScaleZ,

  // TODO:
  // Camera.Fov
  // Light.Intensity
  // Material.ParamX
};

// Track = curve bound to entity + channel
struct AnimTrack {
  EntityID entity = InvalidEntity;
  uint32_t blockId = 0;
  AnimChannel channel{};
  AnimCurve curve;
};

// Per-entity time range within a clip
struct AnimEntityRange {
  EntityID entity = InvalidEntity;
  uint32_t blockId = 0;
  AnimFrame start = 0;
  AnimFrame end = 0;
};

// Clip
struct AnimationClip {
  std::string name;
  AnimFrame lastFrame = 0; // dynamic
  bool loop = true;

  std::vector<AnimTrack> tracks;
  std::vector<AnimEntityRange> entityRanges;
  uint32_t nextBlockId = 1;
};

// What property is animated

} // namespace Nyx
