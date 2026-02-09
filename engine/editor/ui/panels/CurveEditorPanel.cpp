#include "CurveEditorPanel.h"

#include <algorithm>
#include <cmath>

namespace Nyx {

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

} // namespace Nyx
