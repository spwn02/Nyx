#include "CurveEditorPanel.h"

#include "animation/AnimationTypes.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Nyx {

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
  if (t < 1.0f / d1) {
    return n1 * t * t;
  } else if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  } else if (t < 2.5f / d1) {
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

static float evalSegmentEaseLocal(SegmentEase ease, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
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
    if (t < 0.5f)
      return 0.5f * easeBackIn(2.0f * t);
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

void CurveEditorPanel::drawGrid(const ImRect &r) const {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float stepY = m_pixelsPerValue;
  if (stepY < 1.0f)
    return;

  const int32_t startFrame = std::max<int32_t>(0, m_firstFrame);
  const int32_t stepFrame = std::max<int32_t>(1, (int32_t)std::round(10.0f));
  const int32_t firstTick = (startFrame / stepFrame) * stepFrame;
  for (int32_t f = firstTick;; f += stepFrame) {
    const float x = frameToX(f, r.Min.x);
    if (x > r.Max.x)
      break;
    if (x < r.Min.x)
      continue;
    dl->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y),
                IM_COL32(38, 38, 38, 255));
  }

  float y0 = std::fmod(r.Min.y + m_panY, stepY);
  if (y0 > r.Min.y)
    y0 -= stepY;
  for (float y = y0; y < r.Max.y; y += stepY)
    dl->AddLine(ImVec2(r.Min.x, y), ImVec2(r.Max.x, y),
                IM_COL32(38, 38, 38, 255));
}

void CurveEditorPanel::drawCurve(const ImRect &r) const {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return;
  const auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  const auto &keys = curve.keys;
  if (keys.size() < 2)
    return;

  auto cubic = [](float p0, float p1, float p2, float p3, float t) -> float {
    const float u = 1.0f - t;
    return u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 +
           t * t * t * p3;
  };

  ImDrawList *dl = ImGui::GetWindowDrawList();
  for (size_t i = 1; i < keys.size(); ++i) {
    const auto &a = keys[i - 1];
    const auto &b = keys[i];
    if (a.easeOut != SegmentEase::None) {
      ImVec2 prev(frameToXf((float)a.frame, r.Min.x), valueToY(a.value, r.Max.y));
      const int steps = 30;
      for (int si = 1; si <= steps; ++si) {
        const float t = (float)si / (float)steps;
        const float x = (float)a.frame + ((float)b.frame - (float)a.frame) * t;
        const float y =
            a.value + (b.value - a.value) * evalSegmentEaseLocal(a.easeOut, t);
        ImVec2 p(frameToXf(x, r.Min.x), valueToY(y, r.Max.y));
        dl->AddLine(prev, p, IM_COL32(255, 200, 100, 255), 2.0f);
        prev = p;
      }
    } else if (curve.interp == InterpMode::Bezier) {
      const float x0 = (float)a.frame;
      const float y0 = a.value;
      float x1 = x0 + a.out.dx;
      const float y1 = y0 + a.out.dy;
      float x2 = (float)b.frame + b.in.dx;
      const float y2 = b.value + b.in.dy;
      const float x3 = (float)b.frame;
      const float y3 = b.value;
      x1 = std::clamp(x1, x0, x3);
      x2 = std::clamp(x2, x0, x3);
      ImVec2 prev(frameToXf(x0, r.Min.x), valueToY(y0, r.Max.y));
      const int steps = 24;
      for (int si = 1; si <= steps; ++si) {
        const float t = (float)si / (float)steps;
        const float x = cubic(x0, x1, x2, x3, t);
        const float y = cubic(y0, y1, y2, y3, t);
        ImVec2 p(frameToXf(x, r.Min.x), valueToY(y, r.Max.y));
        dl->AddLine(prev, p, IM_COL32(255, 200, 100, 255), 2.0f);
        prev = p;
      }
    } else {
      const ImVec2 p0(frameToX(a.frame, r.Min.x), valueToY(a.value, r.Max.y));
      const ImVec2 p1(frameToX(b.frame, r.Min.x), valueToY(b.value, r.Max.y));
      dl->AddLine(p0, p1, IM_COL32(255, 200, 100, 255), 2.0f);
    }
  }
}

void CurveEditorPanel::drawKeys(const ImRect &r) const {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const auto &keys = m_clip->tracks[(size_t)m_trackIndex].curve.keys;
  for (int i = 0; i < (int)keys.size(); ++i) {
    const auto &k = keys[(size_t)i];
    const ImVec2 p(frameToX(k.frame, r.Min.x), valueToY(k.value, r.Max.y));
    const bool selected = isKeySelected(i);
    const bool active = (i == m_activeKey);
    dl->AddCircleFilled(
        p, selected ? 5.0f : 4.0f,
        selected ? IM_COL32(255, 235, 130, 255) : IM_COL32(240, 240, 240, 255));
    dl->AddCircle(
        p, active ? 6.5f : (selected ? 6.0f : 5.0f),
        active ? IM_COL32(255, 170, 60, 255) : IM_COL32(60, 60, 60, 255));
  }

  const auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  if (m_activeKey < 0 || m_activeKey >= (int)keys.size())
    return;

  const AnimKey &k = keys[(size_t)m_activeKey];
  auto effectiveHandle = [this](float dx, float dy, bool inHandle) -> ImVec2 {
    const float eps = 1e-4f;
    if (std::fabs(dx) < eps && std::fabs(dy) < eps) {
      const float defaultDx =
          std::max(2.0f, 40.0f / std::max(1.0f, m_pixelsPerFrame));
      return ImVec2(inHandle ? -defaultDx : defaultDx, 0.0f);
    }
    return ImVec2(dx, dy);
  };
  const ImVec2 inLocal = effectiveHandle(k.in.dx, k.in.dy, true);
  const ImVec2 outLocal = effectiveHandle(k.out.dx, k.out.dy, false);
  const ImVec2 kc(frameToX(k.frame, r.Min.x), valueToY(k.value, r.Max.y));
  const ImVec2 kin(frameToXf((float)k.frame + inLocal.x, r.Min.x),
                   valueToY(k.value + inLocal.y, r.Max.y));
  const ImVec2 kout(frameToXf((float)k.frame + outLocal.x, r.Min.x),
                    valueToY(k.value + outLocal.y, r.Max.y));
  dl->AddLine(kc, kin, IM_COL32(110, 170, 210, 180), 1.5f);
  dl->AddLine(kc, kout, IM_COL32(110, 170, 210, 180), 1.5f);
  dl->AddCircleFilled(kin, 4.0f, IM_COL32(225, 235, 245, 255));
  dl->AddCircle(kin, 5.0f, IM_COL32(70, 110, 145, 255), 0, 1.2f);
  dl->AddCircleFilled(kout, 4.0f, IM_COL32(225, 235, 245, 255));
  dl->AddCircle(kout, 5.0f, IM_COL32(70, 110, 145, 255), 0, 1.2f);
}

void CurveEditorPanel::drawCurrentFrameLine(const ImRect &r) const {
  const float x = frameToX(m_currentFrame, r.Min.x);
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y),
                                      IM_COL32(120, 180, 255, 220), 1.8f);
}

CurveEditorPanel::HandleHit CurveEditorPanel::hitTestHandle(
    const ImRect &r, int keyIndex, const ImVec2 &mp) const {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return HandleHit::None;
  const auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  if (keyIndex < 0 || keyIndex >= (int)curve.keys.size())
    return HandleHit::None;
  const AnimKey &k = curve.keys[(size_t)keyIndex];
  auto effectiveHandle = [this](float dx, float dy, bool inHandle) -> ImVec2 {
    const float eps = 1e-4f;
    if (std::fabs(dx) < eps && std::fabs(dy) < eps) {
      const float defaultDx =
          std::max(2.0f, 40.0f / std::max(1.0f, m_pixelsPerFrame));
      return ImVec2(inHandle ? -defaultDx : defaultDx, 0.0f);
    }
    return ImVec2(dx, dy);
  };
  const ImVec2 inLocal = effectiveHandle(k.in.dx, k.in.dy, true);
  const ImVec2 outLocal = effectiveHandle(k.out.dx, k.out.dy, false);
  const ImVec2 kin(frameToXf((float)k.frame + inLocal.x, r.Min.x),
                   valueToY(k.value + inLocal.y, r.Max.y));
  const ImVec2 kout(frameToXf((float)k.frame + outLocal.x, r.Min.x),
                    valueToY(k.value + outLocal.y, r.Max.y));
  const float rin2 =
      (mp.x - kin.x) * (mp.x - kin.x) + (mp.y - kin.y) * (mp.y - kin.y);
  if (rin2 <= 49.0f)
    return HandleHit::In;
  const float rout2 =
      (mp.x - kout.x) * (mp.x - kout.x) + (mp.y - kout.y) * (mp.y - kout.y);
  if (rout2 <= 49.0f)
    return HandleHit::Out;
  return HandleHit::None;
}

void CurveEditorPanel::fitViewToKeys(const ImRect &r, bool selectedOnly) {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return;
  const auto &keys = m_clip->tracks[(size_t)m_trackIndex].curve.keys;
  if (keys.empty())
    return;

  bool have = false;
  int32_t minF = 0, maxF = 0;
  float minV = 0.0f, maxV = 0.0f;
  for (int i = 0; i < (int)keys.size(); ++i) {
    if (selectedOnly && !isKeySelected(i))
      continue;
    const auto &k = keys[(size_t)i];
    if (!have) {
      minF = maxF = (int32_t)k.frame;
      minV = maxV = k.value;
      have = true;
    } else {
      minF = std::min(minF, (int32_t)k.frame);
      maxF = std::max(maxF, (int32_t)k.frame);
      minV = std::min(minV, k.value);
      maxV = std::max(maxV, k.value);
    }
  }
  if (!have)
    return;

  const float margin = 24.0f;
  const float h = std::max(1.0f, r.GetHeight() - margin * 2.0f);
  const float vr = std::max(0.1f, maxV - minV);
  m_pixelsPerValue = std::clamp(h / vr, 0.02f, 600.0f);
  const float targetTopY = r.Min.y + margin;
  m_panY = targetTopY - r.Max.y + maxV * m_pixelsPerValue;
}

} // namespace Nyx
