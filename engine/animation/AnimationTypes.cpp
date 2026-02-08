#include "AnimationTypes.h"
#include <algorithm>
#include <cmath>

namespace Nyx {

static float lerp(float a, float b, float t) { return a + t * (b - a); }
static float cubic(float p0, float p1, float p2, float p3, float t) {
  const float u = 1.0f - t;
  return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 +
         t * t * t * p3;
}

static float easeBackIn(float t) {
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  return c3 * t * t * t - c1 * t * t;
}

static float easeBackOut(float t) {
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  const float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}

static float easeBounceOut(float t) {
  const float n1 = 7.5625f;
  const float d1 = 2.75f;
  if (t < 1.0f / d1)
    return n1 * t * t;
  if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  }
  if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  }
  t -= 2.625f / d1;
  return n1 * t * t + 0.984375f;
}

static float easeBounceIn(float t) { return 1.0f - easeBounceOut(1.0f - t); }

static float easeBounceInOut(float t) {
  return (t < 0.5f) ? (1.0f - easeBounceOut(1.0f - 2.0f * t)) * 0.5f
                    : (1.0f + easeBounceOut(2.0f * t - 1.0f)) * 0.5f;
}

static float evalSegmentEase(SegmentEase ease, float t) {
  switch (ease) {
  case SegmentEase::CubicIn:
    return t * t * t;
  case SegmentEase::CubicOut: {
    const float u = t - 1.0f;
    return u * u * u + 1.0f;
  }
  case SegmentEase::CubicInOut:
    return (t < 0.5f) ? 4.0f * t * t * t
                      : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
  case SegmentEase::QuintIn:
    return t * t * t * t * t;
  case SegmentEase::QuintOut:
    return 1.0f - std::pow(1.0f - t, 5.0f);
  case SegmentEase::QuintInOut:
    return (t < 0.5f) ? 16.0f * std::pow(t, 5.0f)
                      : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
  case SegmentEase::ExponentialIn:
    return (t == 0.0f) ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
  case SegmentEase::ExponentialOut:
    return (t == 1.0f) ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
  case SegmentEase::ExponentialInOut:
    if (t == 0.0f)
      return 0.0f;
    if (t == 1.0f)
      return 1.0f;
    return (t < 0.5f) ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
                      : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
  case SegmentEase::BackIn:
    return easeBackIn(t);
  case SegmentEase::BackOut:
    return easeBackOut(t);
  case SegmentEase::BackInOut:
    if (t < 0.5f) {
      const float u = 2.0f * t;
      return 0.5f * easeBackIn(u);
    }
    return 0.5f + 0.5f * easeBackOut(2.0f * t - 1.0f);
  case SegmentEase::BounceIn:
    return easeBounceIn(t);
  case SegmentEase::BounceOut:
    return easeBounceOut(t);
  case SegmentEase::BounceInOut:
    return easeBounceInOut(t);
  case SegmentEase::None:
  default:
    return t;
  }
}

float AnimCurve::sample(AnimFrame frame) const {
  if (keys.empty())
    return 0.0f;

  if (keys.size() == 1)
    return keys[0].value;

  // Before first
  if (frame <= keys.front().frame)
    return keys.front().value;

  // After last
  if (frame >= keys.back().frame)
    return keys.back().value;

  // Find segment
  for (size_t i = 0; i + 1 < keys.size(); ++i) {
    const AnimKey &a = keys[i];
    const AnimKey &b = keys[i + 1];

    if (frame >= a.frame && frame <= b.frame) {
      // Exact right-key hit should return right key value.
      // This keeps stepped/constant curves correct on key boundaries.
      if (frame == b.frame)
        return b.value;

      if (interp == InterpMode::Constant)
        return a.value;

      const float t = float(frame - a.frame) / float(b.frame - a.frame);

      if (a.easeOut != SegmentEase::None)
        return lerp(a.value, b.value, evalSegmentEase(a.easeOut, t));

      if (interp == InterpMode::Linear)
        return lerp(a.value, b.value, t);

      // Bezier: solve x(t)=frame and evaluate y(t)
      const float x0 = (float)a.frame;
      const float y0 = a.value;
      float x1 = x0 + a.out.dx;
      const float y1 = y0 + a.out.dy;
      float x2 = (float)b.frame + b.in.dx;
      const float y2 = b.value + b.in.dy;
      const float x3 = (float)b.frame;
      const float y3 = b.value;

      // Keep control points inside segment bounds for stable monotonic solve.
      x1 = std::clamp(x1, x0, x3);
      x2 = std::clamp(x2, x0, x3);

      const float targetX = (float)frame;
      float lo = 0.0f, hi = 1.0f;
      for (int it = 0; it < 24; ++it) {
        const float mid = (lo + hi) * 0.5f;
        const float xm = cubic(x0, x1, x2, x3, mid);
        if (xm < targetX)
          lo = mid;
        else
          hi = mid;
      }
      const float tb = (lo + hi) * 0.5f;
      return cubic(y0, y1, y2, y3, tb);
    }
  }

  return keys.back().value;
}

} // namespace Nyx
