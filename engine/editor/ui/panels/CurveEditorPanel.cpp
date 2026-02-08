#include "CurveEditorPanel.h"

#include "animation/AnimationTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>

namespace Nyx {

static float cubicEval(float p0, float p1, float p2, float p3, float t) {
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
  if (t < 1.0f / d1) {
    return n1 * t * t;
  } else if (t < 2.0f / d1) {
    t -= 1.5f / d1;
    return n1 * t * t + 0.75f;
  } else if (t < 2.5f / d1) {
    t -= 2.25f / d1;
    return n1 * t * t + 0.9375f;
  } else {
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
  }
}

static float easeBounceIn(float t) { return 1.0f - easeBounceOut(1.0f - t); }

static float easeBounceInOut(float t) {
  return (t < 0.5f) ? (1.0f - easeBounceOut(1.0f - 2.0f * t)) * 0.5f
                    : (1.0f + easeBounceOut(2.0f * t - 1.0f)) * 0.5f;
}

static float easeEval(const CurveEditorPanel::PresetDef &p, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  if (p.family == CurveEditorPanel::PresetDef::Family::Bounce) {
    if (p.mode == CurveEditorPanel::PresetDef::EaseMode::In)
      return easeBounceIn(t);
    if (p.mode == CurveEditorPanel::PresetDef::EaseMode::Out)
      return easeBounceOut(t);
    return easeBounceInOut(t);
  }
  return cubicEval(0.0f, p.y1, p.y2, 1.0f, t);
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

static SegmentEase toSegmentEase(const CurveEditorPanel::PresetDef &p) {
  using Fam = CurveEditorPanel::PresetDef::Family;
  using Mode = CurveEditorPanel::PresetDef::EaseMode;
  if (p.family == Fam::Bounce) {
    if (p.mode == Mode::In)
      return SegmentEase::BounceIn;
    if (p.mode == Mode::Out)
      return SegmentEase::BounceOut;
    return SegmentEase::BounceInOut;
  }
  if (p.name && std::strstr(p.name, "Cubic") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::CubicIn;
    if (p.mode == Mode::Out)
      return SegmentEase::CubicOut;
    return SegmentEase::CubicInOut;
  }
  if (p.name && std::strstr(p.name, "Quint") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::QuintIn;
    if (p.mode == Mode::Out)
      return SegmentEase::QuintOut;
    return SegmentEase::QuintInOut;
  }
  if (p.name && std::strstr(p.name, "Exponential") == p.name) {
    if (p.mode == Mode::In)
      return SegmentEase::ExponentialIn;
    if (p.mode == Mode::Out)
      return SegmentEase::ExponentialOut;
    return SegmentEase::ExponentialInOut;
  }
  if (p.mode == Mode::In)
    return SegmentEase::BackIn;
  if (p.mode == Mode::Out)
    return SegmentEase::BackOut;
  return SegmentEase::BackInOut;
}

bool CurveEditorPanel::isKeySelected(int keyIndex) const {
  return std::find(m_selectedKeys.begin(), m_selectedKeys.end(), keyIndex) !=
         m_selectedKeys.end();
}

void CurveEditorPanel::selectSingleKey(int keyIndex) {
  m_selectedKeys.clear();
  if (keyIndex >= 0)
    m_selectedKeys.push_back(keyIndex);
  m_activeKey = keyIndex;
}

void CurveEditorPanel::setActiveTrack(int trackIndex) {
  if (m_trackIndex == trackIndex)
    return;
  m_trackIndex = trackIndex;
  m_activeKey = -1;
  m_selectedKeys.clear();
  m_draggingKey = false;
  m_draggingHandle = HandleHit::None;
  m_boxSelecting = false;
  m_fitPending = true;
}

void CurveEditorPanel::setFrameWindow(int32_t firstFrame, float pixelsPerFrame) {
  m_firstFrame = std::max<int32_t>(0, firstFrame);
  m_pixelsPerFrame = std::max(1.0f, pixelsPerFrame);
}

float CurveEditorPanel::frameToX(int32_t frame, float x0) const {
  return x0 + (float)(frame - m_firstFrame) * m_pixelsPerFrame;
}

float CurveEditorPanel::frameToXf(float frame, float x0) const {
  return x0 + (frame - (float)m_firstFrame) * m_pixelsPerFrame;
}

float CurveEditorPanel::valueToY(float value, float y0) const {
  return y0 - value * m_pixelsPerValue + m_panY;
}

int32_t CurveEditorPanel::xToFrame(float x, float x0) const {
  return m_firstFrame + (int32_t)std::round((x - x0) / m_pixelsPerFrame);
}

float CurveEditorPanel::yToValue(float y, float y0) const {
  return (y0 - y - m_panY) / m_pixelsPerValue;
}

void CurveEditorPanel::drawGrid(const ImRect &r) const {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float stepX = std::max(1.0f, m_pixelsPerFrame * 10.0f);
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
    dl->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y), IM_COL32(38, 38, 38, 255));
  }

  float y0 = std::fmod(r.Min.y + m_panY, stepY);
  if (y0 > r.Min.y)
    y0 -= stepY;
  for (float y = y0; y < r.Max.y; y += stepY)
    dl->AddLine(ImVec2(r.Min.x, y), ImVec2(r.Max.x, y), IM_COL32(38, 38, 38, 255));
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
      const float defaultDx = std::max(2.0f, 40.0f / std::max(1.0f, m_pixelsPerFrame));
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
      const float defaultDx = std::max(2.0f, 40.0f / std::max(1.0f, m_pixelsPerFrame));
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
  const float rin2 = (mp.x - kin.x) * (mp.x - kin.x) + (mp.y - kin.y) * (mp.y - kin.y);
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

void CurveEditorPanel::onImGui() {
  bool fitAll = false;
  bool fitSel = false;
  if (ImGui::Button("Fit All"))
    fitAll = true;
  ImGui::SameLine();
  if (ImGui::Button("Fit Selected"))
    fitSel = true;
  ImGui::SameLine();
  if (ImGui::Button("Reset View")) {
    m_pixelsPerValue = 24.0f;
    m_panY = 0.0f;
  }
  ImGui::SameLine();
  if (ImGui::Button("Presets"))
    m_showPresetPanel = !m_showPresetPanel;
  ImGui::Separator();

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x <= 2.0f || avail.y <= 2.0f)
    return;

  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImRect r(p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
  const ImRect drawRect(ImVec2(r.Min.x + 1.0f, r.Min.y + 1.0f),
                        ImVec2(r.Max.x - 1.0f, r.Max.y - 1.0f));
  float clipEndX = drawRect.Max.x;
  if (m_clip) {
    // Match sequencer timeline semantics: right boundary is at (lastFrame + 1).
    clipEndX =
        std::min(clipEndX, frameToX(m_clip->lastFrame + 1, drawRect.Min.x));
  }
  const ImRect animRect(drawRect.Min,
                        ImVec2(std::max(drawRect.Min.x, clipEndX), drawRect.Max.y));

  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(r.Min, r.Max, IM_COL32(15, 15, 15, 255));
  dl->AddRect(r.Min, r.Max, IM_COL32(70, 70, 70, 255));
  dl->PushClipRect(drawRect.Min, drawRect.Max, true);
  if (fitAll)
    fitViewToKeys(drawRect, false);
  if (fitSel)
    fitViewToKeys(drawRect, true);
  if (m_fitPending) {
    fitViewToKeys(drawRect, false);
    m_fitPending = false;
  }
  drawGrid(drawRect);
  drawCurrentFrameLine(drawRect);
  dl->PushClipRect(animRect.Min, animRect.Max, true);
  drawCurve(drawRect);
  drawKeys(drawRect);
  dl->PopClipRect();
  if (m_clip) {
    dl->AddLine(ImVec2(clipEndX, drawRect.Min.y), ImVec2(clipEndX, drawRect.Max.y),
                IM_COL32(190, 120, 80, 220), 1.5f);
  }
  dl->PopClipRect();

  ImGui::InvisibleButton("##CurveEditorCanvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 mp = ImGui::GetMousePos();
  const ImGuiIO &io = ImGui::GetIO();
  const bool panModifier = io.KeyAlt || ImGui::IsKeyDown(ImGuiKey_Space);

  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
  {
    dl->AddText(ImVec2(r.Min.x + 12.0f, r.Min.y + 12.0f),
                IM_COL32(160, 160, 160, 255),
                "Select a property channel to edit its curve.");
    return;
  }
  auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  auto &keys = curve.keys;
  if (keys.empty()) {
    dl->AddText(ImVec2(r.Min.x + 12.0f, r.Min.y + 12.0f),
                IM_COL32(160, 160, 160, 255),
                "Selected channel has no keyframes.");
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && panModifier) {
    m_panning = true;
    m_boxSelecting = false;
    m_draggingKey = false;
    m_draggingHandle = HandleHit::None;
  } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (m_activeKey >= 0) {
      const HandleHit h = hitTestHandle(r, m_activeKey, mp);
      if (h != HandleHit::None) {
        m_draggingHandle = h;
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
        const ImVec2 hLocal =
            (h == HandleHit::In) ? effectiveHandle(k.in.dx, k.in.dy, true)
                                 : effectiveHandle(k.out.dx, k.out.dy, false);
        const float cursorDx =
            ((mp.x - r.Min.x) / std::max(1.0f, m_pixelsPerFrame) + (float)m_firstFrame) -
            (float)k.frame;
        const float cursorDy = yToValue(mp.y, r.Max.y) - k.value;
        m_dragHandleOffsetDx = hLocal.x - cursorDx;
        m_dragHandleOffsetDy = hLocal.y - cursorDy;
        m_draggingKey = false;
        m_boxSelecting = false;
        return;
      }
    }
    int hitKey = -1;
    for (int i = 0; i < (int)keys.size(); ++i) {
      const ImVec2 p(frameToX(keys[(size_t)i].frame, r.Min.x),
                     valueToY(keys[(size_t)i].value, r.Max.y));
      const float dx = p.x - mp.x;
      const float dy = p.y - mp.y;
      if (dx * dx + dy * dy <= 36.0f) {
        hitKey = i;
        m_draggingKey = true;
        break;
      }
    }
    if (hitKey >= 0) {
      if (ImGui::GetIO().KeyCtrl) {
        if (isKeySelected(hitKey)) {
          m_selectedKeys.erase(
              std::remove(m_selectedKeys.begin(), m_selectedKeys.end(), hitKey),
              m_selectedKeys.end());
          m_activeKey = m_selectedKeys.empty() ? -1 : m_selectedKeys.back();
        } else {
          m_selectedKeys.push_back(hitKey);
          m_activeKey = hitKey;
        }
      } else {
        selectSingleKey(hitKey);
      }
      m_dragKeyOffsetFrame =
          (int32_t)keys[(size_t)hitKey].frame - xToFrame(mp.x, r.Min.x);
      m_dragKeyOffsetValue =
          keys[(size_t)hitKey].value - yToValue(mp.y, r.Max.y);
    } else {
      m_draggingKey = false;
      m_boxSelecting = true;
      m_boxSelectAdditive = io.KeyCtrl || io.KeyShift;
      m_boxStart = mp;
      m_boxEnd = mp;
      if (!m_boxSelectAdditive) {
        m_selectedKeys.clear();
        m_activeKey = -1;
      }
    }
  }

  if (m_draggingHandle != HandleHit::None && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      m_activeKey >= 0 && m_activeKey < (int)keys.size()) {
    AnimKey &k = keys[(size_t)m_activeKey];
    const float frameF = (mp.x - r.Min.x) / m_pixelsPerFrame + (float)m_firstFrame;
    const float valueF = yToValue(mp.y, r.Max.y);
    float dx = frameF - (float)k.frame + m_dragHandleOffsetDx;
    float dy = valueF - k.value + m_dragHandleOffsetDy;
    const float eps = 0.05f;
    curve.interp = InterpMode::Bezier;
    if (m_draggingHandle == HandleHit::In) {
      dx = std::min(dx, -eps);
      k.in.dx = dx;
      k.in.dy = dy;
      k.out.dx = -dx;
      k.out.dy = -dy;
      if (m_activeKey > 0)
        keys[(size_t)(m_activeKey - 1)].easeOut = SegmentEase::None;
    } else if (m_draggingHandle == HandleHit::Out) {
      dx = std::max(dx, eps);
      k.out.dx = dx;
      k.out.dy = dy;
      k.in.dx = -dx;
      k.in.dy = -dy;
      k.easeOut = SegmentEase::None;
    }
  } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    m_draggingHandle = HandleHit::None;
  }

  if (m_draggingKey && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      m_activeKey >= 0 && m_activeKey < (int)keys.size()) {
    int32_t f = xToFrame(mp.x, r.Min.x) + m_dragKeyOffsetFrame;
    float v = yToValue(mp.y, r.Max.y) + m_dragKeyOffsetValue;
    if (ImGui::GetIO().KeyShift)
      v = keys[(size_t)m_activeKey].value + (v - keys[(size_t)m_activeKey].value) * 0.25f;
    if (m_clip)
      f = std::clamp<int32_t>(f, 0, std::max<int32_t>(0, m_clip->lastFrame));
    else
      f = std::max<int32_t>(0, f);
    keys[(size_t)m_activeKey].frame = f;
    keys[(size_t)m_activeKey].value = v;

    // Keep dragged key identity stable while preserving frame ordering.
    while (m_activeKey > 0 &&
           keys[(size_t)m_activeKey].frame <
               keys[(size_t)(m_activeKey - 1)].frame) {
      std::swap(keys[(size_t)m_activeKey], keys[(size_t)(m_activeKey - 1)]);
      --m_activeKey;
    }
    while (m_activeKey + 1 < (int)keys.size() &&
           keys[(size_t)m_activeKey].frame >
               keys[(size_t)(m_activeKey + 1)].frame) {
      std::swap(keys[(size_t)m_activeKey], keys[(size_t)(m_activeKey + 1)]);
      ++m_activeKey;
    }
  } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    m_draggingKey = false;
  }

  if (m_boxSelecting) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_boxEnd = mp;
    } else {
      const ImVec2 bmin(std::min(m_boxStart.x, m_boxEnd.x),
                        std::min(m_boxStart.y, m_boxEnd.y));
      const ImVec2 bmax(std::max(m_boxStart.x, m_boxEnd.x),
                        std::max(m_boxStart.y, m_boxEnd.y));
      const bool valid = (std::fabs(bmax.x - bmin.x) > 2.0f &&
                          std::fabs(bmax.y - bmin.y) > 2.0f);
      if (valid) {
        for (int i = 0; i < (int)keys.size(); ++i) {
          const ImVec2 p(frameToX(keys[(size_t)i].frame, r.Min.x),
                         valueToY(keys[(size_t)i].value, r.Max.y));
          const float kr = 5.0f;
          const bool overlap = (p.x + kr >= bmin.x && p.x - kr <= bmax.x &&
                                p.y + kr >= bmin.y && p.y - kr <= bmax.y);
          if (overlap) {
            if (!isKeySelected(i))
              m_selectedKeys.push_back(i);
            m_activeKey = i;
          }
        }
      }
      m_boxSelecting = false;
    }
  }
  if (m_boxSelecting) {
    const ImVec2 bmin(std::min(m_boxStart.x, m_boxEnd.x),
                      std::min(m_boxStart.y, m_boxEnd.y));
    const ImVec2 bmax(std::max(m_boxStart.x, m_boxEnd.x),
                      std::max(m_boxStart.y, m_boxEnd.y));
    dl->AddRectFilled(bmin, bmax, IM_COL32(5, 130, 255, 64));
    dl->AddRect(bmin, bmax, IM_COL32(5, 130, 255, 128));
  }

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    AnimKey k{};
    k.frame = std::max<int32_t>(0, xToFrame(mp.x, r.Min.x));
    if (m_clip)
      k.frame = std::clamp<int32_t>(k.frame, 0, std::max<int32_t>(0, m_clip->lastFrame));
    k.easeOut = SegmentEase::None;

    // Stable insert behavior: by default insert on existing curve value at frame.
    // Hold Shift to place key at mouse Y explicitly.
    if (ImGui::GetIO().KeyShift)
      k.value = yToValue(mp.y, r.Max.y);
    else
      k.value = curve.sample(k.frame);

    // If key already exists at this frame, just update/select it.
    int existing = -1;
    for (int i = 0; i < (int)keys.size(); ++i) {
      if ((int32_t)keys[(size_t)i].frame == k.frame) {
        existing = i;
        break;
      }
    }
    if (existing >= 0) {
      keys[(size_t)existing].value = k.value;
      selectSingleKey(existing);
      return;
    }

    keys.push_back(k);
    std::sort(keys.begin(), keys.end(),
              [](const AnimKey &a, const AnimKey &b) { return a.frame < b.frame; });
    for (int i = 0; i < (int)keys.size(); ++i) {
      if ((int32_t)keys[(size_t)i].frame == k.frame) {
        selectSingleKey(i);
        break;
      }
    }
  }

  if (hovered && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X))) {
    if (!m_selectedKeys.empty()) {
      std::sort(m_selectedKeys.begin(), m_selectedKeys.end());
      m_selectedKeys.erase(std::unique(m_selectedKeys.begin(), m_selectedKeys.end()),
                           m_selectedKeys.end());
      for (int i = (int)m_selectedKeys.size() - 1; i >= 0; --i) {
        const int ki = m_selectedKeys[(size_t)i];
        if (ki >= 0 && ki < (int)keys.size())
          keys.erase(keys.begin() + (ptrdiff_t)ki);
      }
      m_selectedKeys.clear();
      m_activeKey = -1;
    } else if (m_activeKey >= 0 && m_activeKey < (int)keys.size()) {
      keys.erase(keys.begin() + (ptrdiff_t)m_activeKey);
      m_activeKey = -1;
    }
  }
  if (hovered && ImGui::IsKeyPressed(ImGuiKey_A)) {
    m_selectedKeys.clear();
    m_selectedKeys.reserve(keys.size());
    for (int i = 0; i < (int)keys.size(); ++i)
      m_selectedKeys.push_back(i);
    m_activeKey = keys.empty() ? -1 : 0;
  }
  if (hovered && ImGui::IsKeyPressed(ImGuiKey_1))
    curve.interp = InterpMode::Bezier;
  if (hovered && ImGui::IsKeyPressed(ImGuiKey_2))
    curve.interp = InterpMode::Linear;
  if (hovered && ImGui::IsKeyPressed(ImGuiKey_3))
    curve.interp = InterpMode::Constant;

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    m_panning = true;
  const bool panningHeld =
      ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
      (ImGui::IsMouseDown(ImGuiMouseButton_Left) && panModifier);
  if (m_panning && panningHeld) {
    const ImVec2 d = ImGui::GetIO().MouseDelta;
    m_panY += d.y;
  } else if (!panningHeld) {
    m_panning = false;
  }

  if (hovered) {
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f) {
      const float s = 1.0f + wheel * 0.1f;
      m_pixelsPerValue = std::clamp(m_pixelsPerValue * s, 0.02f, 300.0f);
    }
  }

  drawPresetPanel();
}

void CurveEditorPanel::drawPresetPreview(const PresetDef &p, const ImVec2 &size) {
  ImGui::InvisibleButton("##PresetPreview", size);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetItemRectMin();
  const ImVec2 b = ImGui::GetItemRectMax();
  dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 255));
  dl->AddRect(a, b, IM_COL32(70, 70, 70, 255));
  const float w = std::max(1.0f, b.x - a.x - 6.0f);
  const float h = std::max(1.0f, b.y - a.y - 6.0f);
  ImVec2 prev(a.x + 3.0f, b.y - 3.0f);
  for (int i = 1; i <= 30; ++i) {
    const float t = (float)i / 30.0f;
    const float x = t;
    const float y = easeEval(p, t);
    const ImVec2 cur(a.x + 3.0f + x * w, b.y - 3.0f - y * h);
    dl->AddLine(prev, cur, IM_COL32(255, 200, 110, 255), 1.8f);
    prev = cur;
  }
}

void CurveEditorPanel::applyPresetToActiveTrack(const PresetDef &p) {
  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size())
    return;
  auto &curve = m_clip->tracks[(size_t)m_trackIndex].curve;
  auto &keys = curve.keys;
  if (keys.size() < 2)
    return;

  std::vector<int> idx;
  if (m_selectedKeys.size() >= 2) {
    idx = m_selectedKeys;
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
  } else {
    idx.reserve(keys.size());
    for (int i = 0; i < (int)keys.size(); ++i)
      idx.push_back(i);
  }
  if (idx.size() < 2)
    return;

  struct Span {
    int32_t af = 0;
    int32_t bf = 0;
    float av = 0.0f;
    float bv = 0.0f;
    int ia = -1;
    int ib = -1;
  };
  std::vector<Span> spans;
  for (size_t ii = 1; ii < idx.size(); ++ii) {
    const int ia = idx[ii - 1];
    const int ib = idx[ii];
    if (ia < 0 || ib < 0 || ia >= (int)keys.size() || ib >= (int)keys.size() || ia == ib)
      continue;
    const AnimKey &a = keys[(size_t)ia];
    const AnimKey &b = keys[(size_t)ib];
    if (b.frame <= a.frame)
      continue;
    spans.push_back(Span{a.frame, b.frame, a.value, b.value, ia, ib});
  }

  const SegmentEase ease = toSegmentEase(p);
  for (const Span &s : spans) {
    AnimKey &a = keys[(size_t)s.ia];
    AnimKey &b = keys[(size_t)s.ib];
    a.easeOut = ease;
    // Keep tangents clean; presets are represented as ghost segment easings.
    a.out.dx = a.out.dy = 0.0f;
    b.in.dx = b.in.dy = 0.0f;
  }
  curve.interp = InterpMode::Linear;
}

void CurveEditorPanel::drawPresetPanel() {
  if (!m_showPresetPanel)
    return;

  static const PresetDef presets[] = {
      {"Cubic In", 0.55f, 0.055f, 0.675f, 0.19f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Cubic Out", 0.215f, 0.61f, 0.355f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Cubic InOut", 0.645f, 0.045f, 0.355f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Quint In", 0.755f, 0.05f, 0.855f, 0.06f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Quint Out", 0.23f, 1.0f, 0.32f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Quint InOut", 0.86f, 0.0f, 0.07f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Exponential In", 0.95f, 0.05f, 0.795f, 0.035f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Exponential Out", 0.19f, 1.0f, 0.22f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Exponential InOut", 1.0f, 0.0f, 0.0f, 1.0f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Back In", 0.6f, -0.28f, 0.735f, 0.045f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::In},
      {"Back Out", 0.175f, 0.885f, 0.32f, 1.275f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::Out},
      {"Back InOut", 0.68f, -0.55f, 0.265f, 1.55f, PresetDef::Family::Bezier,
       PresetDef::EaseMode::InOut},
      {"Bounce In", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::In},
      {"Bounce Out", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::Out},
      {"Bounce InOut", 0.0f, 0.0f, 1.0f, 1.0f, PresetDef::Family::Bounce,
       PresetDef::EaseMode::InOut},
  };

  if (!ImGui::Begin("Curve Presets", &m_showPresetPanel)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted(
      "Apply to selected key spans (or full track if no multi-key selection).");
  ImGui::Separator();
  ImGui::BeginChild("##PresetList", ImVec2(0.0f, 0.0f), false,
                    ImGuiWindowFlags_AlwaysVerticalScrollbar);

  for (int i = 0; i < (int)(sizeof(presets) / sizeof(presets[0])); ++i) {
    ImGui::PushID(i);
    drawPresetPreview(presets[i], ImVec2(110.0f, 34.0f));
    ImGui::SameLine();
    if (ImGui::Button(presets[i].name, ImVec2(180.0f, 34.0f))) {
      applyPresetToActiveTrack(presets[i]);
    }
    ImGui::PopID();
  }

  ImGui::EndChild();
  ImGui::End();
}

} // namespace Nyx
