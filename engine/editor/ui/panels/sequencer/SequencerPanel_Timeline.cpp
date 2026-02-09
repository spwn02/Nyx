#include "editor/ui/panels/SequencerPanel.h"

#include "animation/AnimationSystem.h"
#include "scene/World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_set>
#include <imgui.h>

namespace Nyx {

namespace {
static int32_t clampi(int32_t v, int32_t a, int32_t b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static float clampf(float v, float a, float b) {
  if (v < a)
    return a;
  if (v > b)
    return b;
  return v;
}

static bool propertyHasAnimChannels(SeqProperty prop) {
  return prop == SeqProperty::Position || prop == SeqProperty::Rotation ||
         prop == SeqProperty::Scale;
}
} // namespace

void SequencerPanel::drawMarkers(const ImRect &r, int32_t firstFrame,
                                 int32_t lastFrame) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float xStart = r.Min.x + m_labelGutter;
  for (const SeqMarker &m : m_markers) {
    if (m.frame < firstFrame || m.frame > lastFrame)
      continue;
    const float x = frameToX(m.frame, firstFrame, xStart);
    dl->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y),
                IM_COL32(255, 215, 64, 180), 1.0f);
    if (!m.label.empty()) {
      dl->AddText(ImVec2(x + 4.0f, r.Min.y + 2.0f), IM_COL32(255, 230, 120, 220),
                  m.label.c_str());
    }
  }
}

void SequencerPanel::drawKeysAndTracks(const ImRect &r, int32_t firstFrame,
                                       int32_t lastFrame) {
  if (!m_clip)
    return;

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImRect tracks(ImVec2(r.Min.x + m_labelGutter, r.Min.y + m_rulerHeight),
                      r.Max);
  const float xStart = tracks.Min.x;

  std::unordered_set<uint64_t> selectedEntityFrame;
  selectedEntityFrame.reserve(m_selectedKeys.size() * 2 + 1);
  auto packEntityFrame = [](EntityID e, int32_t frame) -> uint64_t {
    const uint64_t ent =
        (uint64_t(e.generation) << 32) | uint64_t(e.index);
    return (ent << 32) ^ uint64_t(uint32_t(frame));
  };
  for (const SeqKeyRef &sel : m_selectedKeys) {
    if (sel.trackIndex < 0 || sel.trackIndex >= (int)m_clip->tracks.size())
      continue;
    const auto &track = m_clip->tracks[(size_t)sel.trackIndex];
    const auto &keys = track.curve.keys;
    if (sel.keyIndex < 0 || sel.keyIndex >= (int)keys.size())
      continue;
    selectedEntityFrame.insert(
        packEntityFrame(track.entity, (int32_t)keys[(size_t)sel.keyIndex].frame));
  }

  // Background rows + keys.
  for (size_t ri = 0; ri < m_rows.size(); ++ri) {
    const SeqRow &row = m_rows[ri];
    const float y0 = tracks.Min.y + float(ri) * m_rowHeight;
    const float y1 = y0 + m_rowHeight;
    if (y1 < tracks.Min.y || y0 > tracks.Max.y)
      continue;

    const ImU32 bg = (ri & 1u) ? IM_COL32(24, 24, 24, 255) : IM_COL32(28, 28, 28, 255);
    dl->AddRectFilled(ImVec2(tracks.Min.x, y0), ImVec2(tracks.Max.x, y1), bg);
    dl->AddLine(ImVec2(tracks.Min.x, y1), ImVec2(tracks.Max.x, y1),
                IM_COL32(48, 48, 48, 255), 1.0f);

    if (row.type != SeqRowType::Property)
      continue;

    if (!findPropertyKeys(row.entity, row.prop, m_frameScratch))
      continue;

    const float cy = 0.5f * (y0 + y1);
    for (int32_t f : m_frameScratch) {
      if (f < firstFrame || f > lastFrame)
        continue;
      const float x = frameToX(f, firstFrame, xStart);
      if (x < tracks.Min.x || x > tracks.Max.x)
        continue;

      const bool selected =
          selectedEntityFrame.find(packEntityFrame(row.entity, f)) !=
          selectedEntityFrame.end();

      dl->AddCircleFilled(ImVec2(x, cy), selected ? 5.0f : 4.0f,
                          selected ? IM_COL32(255, 190, 64, 255)
                                   : IM_COL32(170, 170, 170, 255));
    }
  }

  // Current frame line.
  if (m_anim) {
    const float x = frameToX(m_anim->frame(), firstFrame, xStart);
    dl->AddLine(ImVec2(x, r.Min.y), ImVec2(x, r.Max.y),
                IM_COL32(255, 90, 90, 255), 2.0f);
  }

  // Selection + drag.
  const ImVec2 mouse = ImGui::GetMousePos();
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_timelineHovered) {
    SeqKeyRef hit{};
    if (hitTestKey(r, firstFrame, mouse, hit)) {
      if (ImGui::GetIO().KeyShift)
        addSelect(hit);
      else
        selectSingle(hit);

      if (hit.trackIndex >= 0 && hit.trackIndex < (int)m_clip->tracks.size()) {
        const auto &keys = m_clip->tracks[(size_t)hit.trackIndex].curve.keys;
        if (hit.keyIndex >= 0 && hit.keyIndex < (int)keys.size()) {
          m_draggingKey = true;
          m_dragStartFrame = xToFrame(mouse.x, firstFrame, xStart);
          m_dragOrigKeyFrame = (int32_t)keys[(size_t)hit.keyIndex].frame;
        }
      }
    } else if (!ImGui::GetIO().KeyShift) {
      clearSelection();
    }
  }

  if (m_draggingKey && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    const int32_t toFrame = clampFrame(xToFrame(mouse.x, firstFrame, xStart));
    const int32_t delta = toFrame - m_dragStartFrame;
    if (delta != 0 && m_activeKey.trackIndex >= 0) {
      moveKeyFrame(m_activeKey, m_dragOrigKeyFrame + delta);
      if (m_autoUpdateLastFrame)
        recomputeLastFrameFromKeys();
    }
    m_draggingKey = false;
  }
}

void SequencerPanel::drawTimeline() {
  if (!m_clip || !m_anim) {
    ImGui::TextUnformatted("No clip bound.");
    return;
  }

  rebuildLayoutCacheIfNeeded();

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const ImRect rect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y));
  ImDrawList *dl = ImGui::GetWindowDrawList();

  dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(22, 22, 22, 255));
  dl->AddRect(rect.Min, rect.Max, IM_COL32(60, 60, 60, 255));

  const int32_t lastFrame = std::max<int32_t>(0, m_clip->lastFrame);
  const float timelineWidth = std::max(1.0f, rect.GetWidth() - m_labelGutter);
  m_minPixelsPerFrame = std::max(1.0f, timelineWidth / std::max(1, lastFrame + 1));
  if (m_pixelsPerFrame < m_minPixelsPerFrame)
    m_pixelsPerFrame = m_minPixelsPerFrame;

  const int32_t framesVisible =
      std::max<int32_t>(1, (int32_t)(timelineWidth / std::max(1.0f, m_pixelsPerFrame)));
  const int32_t maxFirstFrame = std::max(0, lastFrame - framesVisible);
  m_viewFirstFrame = clampi(m_viewFirstFrame, 0, maxFirstFrame);
  const int32_t firstFrame = m_viewFirstFrame;
  const int32_t lastVisible =
      std::min(lastFrame, firstFrame + std::max<int32_t>(0, framesVisible - 1));

  // Ruler.
  const ImRect ruler(rect.Min, ImVec2(rect.Max.x, rect.Min.y + m_rulerHeight));
  dl->AddRectFilled(ruler.Min, ruler.Max, IM_COL32(18, 18, 18, 255));
  dl->AddLine(ImVec2(ruler.Min.x + m_labelGutter, ruler.Min.y),
              ImVec2(ruler.Min.x + m_labelGutter, ruler.Max.y),
              IM_COL32(70, 70, 70, 255), 1.0f);

  const float xStart = rect.Min.x + m_labelGutter;
  const int32_t tickStep = std::max<int32_t>(1, (int32_t)std::round(70.0f / m_pixelsPerFrame));
  const int32_t firstTick = (firstFrame / tickStep) * tickStep;
  for (int32_t f = firstTick; f <= lastVisible; f += tickStep) {
    const float x = frameToX(f, firstFrame, xStart);
    dl->AddLine(ImVec2(x, ruler.Min.y), ImVec2(x, rect.Max.y), IM_COL32(45, 45, 45, 255),
                (f % 10 == 0) ? 1.3f : 1.0f);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", f);
    dl->AddText(ImVec2(x + 2.0f, ruler.Min.y + 2.0f), IM_COL32(150, 150, 150, 255), buf);
  }

  drawMarkers(rect, firstFrame, lastVisible);
  drawKeysAndTracks(rect, firstFrame, lastVisible);

  ImGui::SetCursorScreenPos(rect.Min);
  ImGui::InvisibleButton("##SeqTimelineHit", rect.GetSize(),
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
  m_timelineHovered = ImGui::IsItemHovered();
  m_timelineActive = ImGui::IsItemActive();
  const ImVec2 mouse = ImGui::GetMousePos();

  if (m_timelineHovered) {
    ImGuiIO &io = ImGui::GetIO();

    if (io.KeyAlt && io.MouseWheel != 0.0f) {
      const float zoom = (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
      m_pixelsPerFrame = std::max(m_minPixelsPerFrame, m_pixelsPerFrame * zoom);
    } else if ((io.MouseWheelH != 0.0f) || (io.KeyShift && io.MouseWheel != 0.0f)) {
      const float scroll = (io.MouseWheelH != 0.0f) ? io.MouseWheelH : io.MouseWheel;
      const int32_t step = std::max<int32_t>(1, framesVisible / 10);
      m_viewFirstFrame = clampi(m_viewFirstFrame - (int32_t)std::round(scroll * (float)step),
                                0, maxFirstFrame);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const int32_t f = clampFrame(firstFrame + (int32_t)std::round(
                                                    (mouse.x - xStart) /
                                                    std::max(1.0f, m_pixelsPerFrame)));
      m_anim->setFrame(f);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
      m_panningTimeline = true;
      m_panStartMouseX = mouse.x;
      m_panStartFirstFrame = m_viewFirstFrame;
    }
  }

  if (m_panningTimeline && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    const float dx = mouse.x - m_panStartMouseX;
    const int32_t df = (int32_t)std::round(-dx / std::max(1.0f, m_pixelsPerFrame));
    m_viewFirstFrame = clampi(m_panStartFirstFrame + df, 0, maxFirstFrame);
  } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    m_panningTimeline = false;
  }
}

void SequencerPanel::drawLayerBarPane() {
  if (!m_world)
    return;

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImRect rect(p0, ImVec2(p0.x + avail.x, p0.y + avail.y));

  dl->AddRectFilled(rect.Min, rect.Max, IM_COL32(24, 24, 24, 255));

  for (size_t ri = 0; ri < m_rows.size(); ++ri) {
    const SeqRow &r = m_rows[ri];
    const float y = p0.y + m_rulerHeight + float(ri) * m_rowHeight;
    if (y + m_rowHeight < rect.Min.y || y > rect.Max.y)
      continue;

    ImGui::SetCursorScreenPos(ImVec2(p0.x + 8.0f + float(r.depth) * 14.0f, y + 2.0f));

    const uint64_t k = rowKey(r.entity, r.type, r.prop);
    const bool expandable = (r.type == SeqRowType::Layer || r.type == SeqRowType::Group);
    if (expandable) {
      bool ex = false;
      auto it = m_expandState.find(k);
      if (it != m_expandState.end())
        ex = it->second;
      if (ImGui::SmallButton(ex ? "v" : ">")) {
        m_expandState[k] = !ex;
        markLayoutDirty();
      }
      ImGui::SameLine();
    }

    std::string label;
    if (r.type == SeqRowType::Layer) {
      label = (m_world->isAlive(r.entity) ? m_world->name(r.entity).name : "Entity");
    } else if (r.type == SeqRowType::Group) {
      label = "Transform";
    } else if (r.type == SeqRowType::Property) {
      switch (r.prop) {
      case SeqProperty::Position:
        label = "Position";
        break;
      case SeqProperty::Rotation:
        label = "Rotation";
        break;
      case SeqProperty::Scale:
        label = "Scale";
        break;
      case SeqProperty::Opacity:
        label = "Opacity";
        break;
      case SeqProperty::Audio:
        label = "Audio";
        break;
      case SeqProperty::Masks:
        label = "Masks";
        break;
      }
    } else {
      label = "Stub";
    }

    const bool selected = m_selectedLayerBlocks.find(r.entity) != m_selectedLayerBlocks.end();
    if (ImGui::Selectable((label + "##row" + std::to_string(ri)).c_str(), selected,
                          ImGuiSelectableFlags_SpanAllColumns,
                          ImVec2(avail.x - (16.0f + float(r.depth) * 14.0f), m_rowHeight - 2.0f))) {
      if (r.type == SeqRowType::Layer) {
        if (ImGui::GetIO().KeyCtrl) {
          if (selected)
            m_selectedLayerBlocks.erase(r.entity);
          else
            m_selectedLayerBlocks.insert(r.entity);
        } else {
          m_selectedLayerBlocks.clear();
          m_selectedLayerBlocks.insert(r.entity);
        }
      } else if (r.type == SeqRowType::Property) {
        m_graphTrackIndex = graphTrackForPropertyBest(r.entity, r.prop);
      }
    }

    if (r.type == SeqRowType::Property && propertyHasAnimChannels(r.prop)) {
      ImGui::SameLine();
      bool sw = stopwatchEnabled(r.entity, r.prop);
      if (ImGui::Checkbox(("##sw" + std::to_string(ri)).c_str(), &sw)) {
        setStopwatch(r.entity, r.prop, sw);
      }
    }
  }
}

} // namespace Nyx
