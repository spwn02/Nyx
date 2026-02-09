#include "CurveEditorPanel.h"

#include "animation/AnimationTypes.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Nyx {

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

  if (!m_clip || m_trackIndex < 0 || m_trackIndex >= (int)m_clip->tracks.size()) {
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
            ((mp.x - r.Min.x) / std::max(1.0f, m_pixelsPerFrame) +
             (float)m_firstFrame) -
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
      m_dragKeyOffsetValue = keys[(size_t)hitKey].value - yToValue(mp.y, r.Max.y);
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

  if (m_draggingHandle != HandleHit::None &&
      ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_activeKey >= 0 &&
      m_activeKey < (int)keys.size()) {
    AnimKey &k = keys[(size_t)m_activeKey];
    const float frameF =
        (mp.x - r.Min.x) / m_pixelsPerFrame + (float)m_firstFrame;
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
      v = keys[(size_t)m_activeKey].value +
          (v - keys[(size_t)m_activeKey].value) * 0.25f;
    if (m_clip)
      f = std::clamp<int32_t>(f, 0, std::max<int32_t>(0, m_clip->lastFrame));
    else
      f = std::max<int32_t>(0, f);
    keys[(size_t)m_activeKey].frame = f;
    keys[(size_t)m_activeKey].value = v;

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
      k.frame =
          std::clamp<int32_t>(k.frame, 0, std::max<int32_t>(0, m_clip->lastFrame));
    k.easeOut = SegmentEase::None;

    if (ImGui::GetIO().KeyShift)
      k.value = yToValue(mp.y, r.Max.y);
    else
      k.value = curve.sample(k.frame);

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

  if (hovered &&
      (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_X))) {
    if (!m_selectedKeys.empty()) {
      std::sort(m_selectedKeys.begin(), m_selectedKeys.end());
      m_selectedKeys.erase(
          std::unique(m_selectedKeys.begin(), m_selectedKeys.end()),
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

} // namespace Nyx
