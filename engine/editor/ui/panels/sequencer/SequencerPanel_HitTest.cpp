#include "editor/ui/panels/SequencerPanel.h"

#include <imgui.h>

namespace Nyx {

namespace {

bool propertyHasAnimChannels(SeqProperty prop) {
  return prop == SeqProperty::Position || prop == SeqProperty::Rotation ||
         prop == SeqProperty::Scale;
}

bool vecNear(const ImVec2 &a, const ImVec2 &b, float r) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return (dx * dx + dy * dy) <= r * r;
}

} // namespace

bool SequencerPanel::hitTestPropertyKey(const ImRect &r, int32_t firstFrame,
                                        const ImVec2 &mouse,
                                        EntityID &outEntity,
                                        SeqProperty &outProp,
                                        int32_t &outFrame,
                                        SeqKeyRef &outKey) const {
  outEntity = InvalidEntity;
  outProp = SeqProperty::Position;
  outFrame = 0;
  outKey = SeqKeyRef{};
  if (!m_clip)
    return false;

  const float laneH = m_rulerHeight;
  ImRect tracks = r;
  tracks.Min.y += laneH;

  if (mouse.x < (tracks.Min.x + m_labelGutter) || mouse.x > tracks.Max.x ||
      mouse.y < tracks.Min.y || mouse.y > tracks.Max.y)
    return false;

  const int32_t row = (int32_t)((mouse.y - tracks.Min.y) / m_rowHeight);
  if (row < 0 || row >= (int32_t)m_rows.size())
    return false;

  const SeqRow &rr = m_rows[(size_t)row];
  if (rr.type != SeqRowType::Property)
    return false;
  if (!propertyHasAnimChannels(rr.prop))
    return false;

  const float xStart = tracks.Min.x + m_labelGutter;
  const float y0 = tracks.Min.y + float(row) * m_rowHeight;
  const float y1 = y0 + m_rowHeight;
  const float cy = (y0 + y1) * 0.5f;

  if (!buildPropertyFrameCache(rr.entity, rr.prop, m_frameScratch,
                               &m_frameToKeyScratch))
    return false;

  for (int32_t f : m_frameScratch) {
    const float x = frameToX(f, firstFrame, xStart);
    if (!vecNear(mouse, ImVec2(x, cy), 6.0f))
      continue;

    auto it = m_frameToKeyScratch.find(f);
    if (it != m_frameToKeyScratch.end()) {
      outEntity = rr.entity;
      outProp = rr.prop;
      outFrame = f;
      outKey = it->second;
      return true;
    }
  }
  return false;
}

bool SequencerPanel::hitTestKey(const ImRect &r, int32_t firstFrame,
                                const ImVec2 &mouse,
                                SeqKeyRef &outKey) const {
  if (!m_clip)
    return false;

  const float laneH = m_rulerHeight;
  ImRect tracks = r;
  tracks.Min.y += laneH;

  if (mouse.x < (tracks.Min.x + m_labelGutter) || mouse.x > tracks.Max.x ||
      mouse.y < tracks.Min.y || mouse.y > tracks.Max.y)
    return false;

  const int32_t row = (int32_t)((mouse.y - tracks.Min.y) / m_rowHeight);
  if (row < 0 || row >= (int32_t)m_rows.size())
    return false;

  const float xStart = tracks.Min.x + m_labelGutter;
  const SeqRow &rrow = m_rows[(size_t)row];
  if (rrow.type != SeqRowType::Property)
    return false;

  const float y0 = tracks.Min.y + float(row) * m_rowHeight;
  const float y1 = y0 + m_rowHeight;
  const float cy = (y0 + y1) * 0.5f;

  if (!buildPropertyFrameCache(rrow.entity, rrow.prop, m_frameScratch,
                               &m_frameToKeyScratch))
    return false;

  for (int32_t f : m_frameScratch) {
    const float x = frameToX(f, firstFrame, xStart);
    if (!vecNear(mouse, ImVec2(x, cy), 6.0f))
      continue;

    auto it = m_frameToKeyScratch.find(f);
    if (it != m_frameToKeyScratch.end()) {
      outKey = it->second;
      return true;
    }
  }

  return false;
}

} // namespace Nyx
